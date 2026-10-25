#!/usr/bin/env python3
"""Static enumeration of the typed IR's poisoning paths (typed-IR step 21a; docs: the step 21 plan, section 3a).

A POISON is how the builder continues after it has reported a mistake. A SINK is a decision that reads poison and stays silent; a SOURCE is where
poison is made. This tool derives all of them from the clang AST of the builder sources. There is NO list of function or field names in it: the one
fact it starts from is the enumerator IR_T_ERROR (ir.h), the definition of poison, and everything else is a fixpoint over the code:

  aliases     an enumerator X that the code uses to build the poison type:  <array>[X].kind = IR_T_ERROR   (P_ERROR in ir.c)
  predicates  a function returning bool/int whose return value tests a poison enumerator, or calls another predicate  (has_error)
  producers   a function that returns, or builds the value it returns from, a poison enumerator or another producer  (ir_type_error, error_node, is_error)
  carriers    a struct field assigned under a poison test, from a poison test, or on a variable that was initialised by a producer
              (StructInfo.poisoned, Synth.pending ...); a field assigned from a carrier read is a carrier too
  null-poison a function that returns a pointer and returns NULL on a path guarded by a poison test or following a reported error; its callers' NULL
              tests are sinks too (`if (!t) return is_error(...)`)

  atom        the smallest expression that tests poison: `x == IR_T_ERROR`, a predicate call, a carrier read, a NULL test of a null-poison value
  sink        the decision (an if / while / for / ?: condition, or the statement expression) that contains atoms. Its `atoms` carry the source range of
              each atom and the constant that makes it "not poison"; step 21h rewrites exactly those ranges to ablate the sink.
  emitters    every call to ir_error (the only recorder of a diagnostic), with the diag_ir_* code constructor it passes.

Run from the repo root:
  python3 tools/poison_paths.py [--json OUT] [--audit-diff]       print the report (and write the manifest)
  python3 tools/poison_paths.py --check MANIFEST                  fail if the manifest differs from what the code gives now (step 21c)
A hard failure (exit 2), never a skip, if clang is missing, a file does not parse, or the poison aliases cannot be derived.
"""
import hashlib, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.getcwd()
SRCDIR = "tools/mannac"
# Files that are NOT the builder: test programs, the dumper, the harnesses, the self-check, the generated-test driver, the parser/lexer side.
# Everything else named ir*.c is analysed, so a new builder file is included without anyone editing this tool.
EXCLUDE = {"ir_test.c", "ir_ndebug_test.c", "ir_main.c", "ir_dump.c", "ir_selfcheck.c", "ir_e0261_exhaustive.c", "ir_inject.c", "fuzz_ir.c"}
ROOT_ENUM = "IR_T_ERROR"
ASSERT_FUNCS = {"__assert_rtn", "__assert_fail", "__assert", "_wassert", "__assert_perror_fail"}


class Fail(Exception):
    pass


BUILDER = re.compile(r"ir(_[a-z0-9_]+)?\.[ch]")


def norm(path, root):
    """a path as clang printed it -> repo-relative, whatever the toolchain printed (absolute, relative to the cwd, through a symlink such as macOS /var -> /private/var).
    A file outside the repo keeps its absolute real path."""
    if path is None:
        return None
    real = os.path.realpath(path if os.path.isabs(path) else os.path.join(root, path))
    rel = os.path.relpath(real, os.path.realpath(root))
    return real if rel.startswith("..") else rel.replace(os.sep, "/")


def is_builder(rel):
    """a file of the builder: tools/mannac/ir*.c or ir*.h, except the test, dump and harness programs"""
    d, b = os.path.split(rel)
    return d == SRCDIR and BUILDER.fullmatch(b) is not None and b not in EXCLUDE


def builder_files(root=None):
    d = os.path.join(root or ROOT, SRCDIR)
    return sorted(f for f in os.listdir(d) if re.fullmatch(r"ir(_[a-z0-9_]+)?\.c", f) and f not in EXCLUDE)


def ast_of(path, root):
    r = subprocess.run(["clang", "-std=c11", "-fsyntax-only", "-I", os.path.join(root, SRCDIR), "-Xclang", "-ast-dump=json", path],
                       capture_output=True, text=True, cwd=root)
    if r.returncode != 0:
        raise Fail("clang failed on %s:\n%s" % (path, r.stderr[:2000]))
    return json.loads(r.stdout)


# ---- AST helpers ----

def kids(n):
    return n.get("inner", []) if isinstance(n, dict) else []


def walk(n):
    yield n
    for c in kids(n):
        yield from walk(c)


def annotate(tu, root):
    """Give every node the file and byte range of its begin..end (the expansion site for a macro), tracking clang's habit of printing `file` only when it changes."""
    st = {"file": None}

    def loc(l):
        if not isinstance(l, dict):
            return None
        if "expansionLoc" in l:  # a macro: the spelling site may change the printed file, so read it first; the range is the expansion site
            if "spellingLoc" in l:
                loc(l["spellingLoc"])
            return loc(l["expansionLoc"])
        if "file" in l:
            st["file"] = norm(l["file"], root)
        if "offset" in l:
            return (st["file"], l["offset"], l.get("tokLen", 0))
        return None

    def go(n):
        if not isinstance(n, dict):
            return
        a = loc(n.get("loc"))
        rg = n.get("range")
        b = e = None
        if rg:
            b = loc(rg.get("begin"))
            e = loc(rg.get("end"))
        n["_a"] = a
        if b and e and b[0] == e[0]:
            n["_f"], n["_s"], n["_e"] = b[0], b[1], e[1] + e[2]
        for c in kids(n):
            go(c)

    for top in kids(tu):
        go(top)


def strip(n):
    while isinstance(n, dict) and n.get("kind") in ("ImplicitCastExpr", "ParenExpr", "CStyleCastExpr"):
        k = kids(n)
        if not k:
            break
        n = k[0]
    return n


def ref_name(n):
    n = strip(n)
    if isinstance(n, dict) and n.get("kind") == "DeclRefExpr":
        return n.get("referencedDecl", {}).get("name"), n.get("referencedDecl", {}).get("kind")
    return None, None


def is_assert(n):
    return n.get("kind") == "ConditionalOperator" and any(
        d.get("kind") == "DeclRefExpr" and d.get("referencedDecl", {}).get("name") in ASSERT_FUNCS for d in walk(n))


def is_null_value(n):
    n = strip(n)
    if not isinstance(n, dict):
        return False
    if n.get("kind") == "IntegerLiteral" and n.get("value") == "0":
        return True
    if n.get("kind") == "CXXNullPtrLiteralExpr":
        return True
    return n.get("kind") == "ImplicitCastExpr" and n.get("castKind") == "NullToPointer" or \
        (n.get("kind") == "CStyleCastExpr" and any(is_null_value(c) for c in kids(n)))


def callee(n):
    """name of the function a CallExpr calls, or None"""
    n = strip(n) if isinstance(n, dict) else n
    if isinstance(n, dict) and n.get("kind") == "CallExpr":
        k = kids(n)
        if k:
            nm, kd = ref_name(k[0])
            if kd == "FunctionDecl":
                return nm
    return None


class Program:
    def __init__(self, asts, sources, root):
        self.sources = sources  # repo-relative path -> bytes
        self.funcs = {}  # name -> (file, node)   (static functions of the same name in two files: last wins; the build has none today)
        unanalysed = []
        for tu in asts:
            annotate(tu, root)
            for top in kids(tu):
                if top.get("kind") == "FunctionDecl" and any(c.get("kind") == "CompoundStmt" for c in kids(top)):
                    f = top.get("_a") and top["_a"][0]
                    if f and is_builder(f):
                        if top["name"] in self.funcs and self.funcs[top["name"]][0] != f:
                            raise Fail("function %s is defined in %s and %s: the tool keys functions by name" % (top["name"], self.funcs[top["name"]][0], f))
                        self.funcs[top["name"]] = (f, top)
                    elif f and BUILDER.fullmatch(os.path.basename(f)) and os.path.basename(f) not in EXCLUDE:
                        unanalysed.append((top["name"], f))  # a builder-named file the path check did not recognise: found by NAME, so a printing difference cannot hide it
        if unanalysed:
            raise Fail("functions with a body in a builder file or header were NOT analysed (the path %s does not normalise to %s/...): %s" %
                       (unanalysed[0][1], SRCDIR, ", ".join("%s (%s)" % u for u in unanalysed[:5])))
        if not self.funcs:
            raise Fail("no functions found: the AST file tracking failed")
        self.enumvals = {}
        for tu in asts:
            for n in walk(tu):
                if n.get("kind") == "EnumDecl":
                    prev = -1
                    for c in kids(n):
                        if c.get("kind") != "EnumConstantDecl":
                            continue
                        v = next((x.get("value") for x in walk(c) if x.get("kind") in ("ConstantExpr", "IntegerLiteral") and x.get("value") is not None), None)
                        prev = int(v) if v is not None else prev + 1
                        self.enumvals.setdefault(c["name"], prev)
        self.aliases = set()
        self.predicates, self.producers, self.nullpoison, self.reporters = set(), set(), set(), set()
        self.poison_params = {}  # function -> {parameter name}: a parameter that a caller fills with a null-poison value
        self.carriers = set()
        self.derive()

    # ---- text ----
    def text(self, n):
        if "_s" not in n:
            return ""
        src = self.sources.get(n["_f"])
        return re.sub(r"\s+", " ", src[n["_s"]:n["_e"]].decode("utf-8", "replace")).strip() if src else ""

    # ---- poison tests ----
    def is_root(self, n):
        nm, kd = ref_name(n)
        return kd == "EnumConstantDecl" and (nm == ROOT_ENUM or nm in self.aliases)

    def root_compare(self, n):
        n = strip(n)
        return n.get("kind") == "BinaryOperator" and n.get("opcode") in ("==", "!=") and any(self.is_root(c) for c in kids(n))

    def derive(self):
        # 1. aliases: <expr>[X].kind = IR_T_ERROR
        for f, fn in self.funcs.values():
            for n in walk(fn):
                if n.get("kind") == "BinaryOperator" and n.get("opcode") == "=":
                    l, r = kids(n)
                    if ref_name(r) == (ROOT_ENUM, "EnumConstantDecl"):
                        for m in walk(l):
                            if m.get("kind") == "ArraySubscriptExpr":
                                nm, kd = ref_name(kids(m)[1])
                                if kd == "EnumConstantDecl":
                                    self.aliases.add(nm)
        if not self.aliases:
            raise Fail("cannot derive the poison alias enumerator (no `<array>[X].kind = %s` assignment): ir.c prims_init changed shape" % ROOT_ENUM)
        # 2. predicates, producers, null-poison, carriers: one fixpoint
        for _ in range(20):
            self.changed = False
            before = (len(self.predicates), len(self.producers), len(self.nullpoison), len(self.carriers), len(self.reporters))
            for name, (f, fn) in self.funcs.items():
                self.classify(name, fn)
            if before == (len(self.predicates), len(self.producers), len(self.nullpoison), len(self.carriers), len(self.reporters)) and not self.changed:
                return
        raise Fail("poison fixpoint did not converge")

    def ret_kind(self, fn):
        q = fn.get("type", {}).get("qualType", "")
        ret = q.split("(")[0].strip()
        return "ptr" if ret.endswith("*") else "bool" if ret in ("bool", "_Bool", "int") else "struct" if ret else "?"

    def classify(self, name, fn):
        rk = self.ret_kind(fn)
        body = next(c for c in kids(fn) if c.get("kind") == "CompoundStmt")
        if name not in self.reporters and any(callee(x) == "ir_error" or callee(x) in self.reporters for x in walk(body) if x.get("kind") == "CallExpr"):
            self.reporters.add(name)
        rets = [n for n in walk(body) if n.get("kind") == "ReturnStmt" and kids(n)]
        # predicate
        if rk == "bool" and name not in self.predicates:
            for r in rets:
                v = strip(kids(r)[0])
                # the returned VALUE is the test: a comparison / logic chain / negation / predicate call, not a `?:` that picks between two values
                if self.is_test_value(v) and (any(self.root_compare(x) for x in walk(v)) or any(callee(x) in self.predicates for x in walk(v) if x.get("kind") == "CallExpr")):
                    self.predicates.add(name)
        # producer: a return value built from a poison enumerator or a producer call, directly or through a variable initialised from one
        if name not in self.producers and name not in self.predicates:
            vars_poison = set()
            for n in walk(body):
                if n.get("kind") == "VarDecl" and kids(n) and self.makes_poison(kids(n)[0]):
                    vars_poison.add(n.get("name"))
            for r in rets:
                v = kids(r)[0]
                if self.makes_poison(v) or ref_name(v)[0] in vars_poison:
                    self.producers.add(name)
        # null-poison
        if rk in ("ptr", "struct") and name not in self.nullpoison and self.returns_null_after_poison(body):
            self.nullpoison.add(name)
        # parameters of other functions that this one fills with a null-poison value
        nv = self.nullvars_of(body, name)
        for x in walk(body):
            if x.get("kind") == "CallExpr" and callee(x) in self.funcs:
                g = callee(x)
                params = [c.get("name") for c in kids(self.funcs[g][1]) if c.get("kind") == "ParmVarDecl"]
                for i, a in enumerate(kids(x)[1:]):
                    a = strip(a)
                    poison = (a.get("kind") == "CallExpr" and callee(a) in self.nullpoison) or (a.get("kind") == "DeclRefExpr" and ref_name(a)[0] in nv)
                    if poison and i < len(params) and params[i] not in self.poison_params.setdefault(g, set()):
                        self.poison_params[g].add(params[i])
                        self.changed = True
        # carriers
        self.find_carriers(body)

    def is_test_value(self, v):
        v = strip(v)
        k = v.get("kind")
        return (k == "BinaryOperator" and v.get("opcode") in ("==", "!=", "&&", "||", "<", ">", "<=", ">=")) or \
            (k == "UnaryOperator" and v.get("opcode") == "!") or (k == "CallExpr" and callee(v) in self.predicates)

    def makes_poison(self, e):
        for x in walk(e):
            if x.get("kind") == "DeclRefExpr" and self.is_root(x):
                # a comparison with the enumerator is a test, not a production
                return not self.under_compare(e, x)
            if x.get("kind") == "CallExpr" and callee(x) in self.producers:
                return True
        return False

    def under_compare(self, top, target):
        for x in walk(top):
            if self.root_compare(x) and any(y is target for y in walk(x)):
                return True
        return False

    def is_poison_return(self, v):
        """a returned value that means "I could not": NULL, or an enumerator other than the one whose value is 0 (LAY_OK is 0, LAY_UNSIZED is not)"""
        if is_null_value(v):
            return True
        nm, kd = ref_name(v)
        return kd == "EnumConstantDecl" and self.enumvals.get(nm, 0) != 0

    def returns_null_after_poison(self, body):
        def go(n, guarded, reported):
            k = n.get("kind")
            if k == "ReturnStmt" and kids(n) and self.is_poison_return(kids(n)[0]):
                return guarded or reported
            if k == "IfStmt":
                c = kids(n)
                g = guarded or bool(self.atoms_in(c[0], set()))
                return any(go(x, g, reported) for x in c[1:])
            if k == "CompoundStmt":
                rep = reported
                for x in kids(n):
                    if go(x, guarded, rep):
                        return True
                    if any(callee(y) == "ir_error" or callee(y) in self.reporters or callee(y) in self.producers for y in walk(x) if y.get("kind") == "CallExpr"):
                        rep = True
                return False
            return any(go(x, guarded, reported) for x in kids(n)) if k in ("SwitchStmt", "CaseStmt", "DefaultStmt", "WhileStmt", "ForStmt", "DoStmt") else False
        return go(body, False, False)

    def find_carriers(self, body):
        init_poison_vars = set()
        for n in walk(body):
            if n.get("kind") == "VarDecl" and kids(n) and self.makes_poison(kids(n)[0]):
                init_poison_vars.add(n.get("name"))

        def go(n, guarded):
            k = n.get("kind")
            if k == "BinaryOperator" and n.get("opcode") == "=":
                l, r = kids(n)
                l = strip(l)
                if l.get("kind") == "MemberExpr":
                    base = next((ref_name(b)[0] for b in walk(l) if b.get("kind") == "DeclRefExpr"), None)
                    if guarded or self.atoms_in(r, set()) or base in init_poison_vars:
                        self.carriers.add(l.get("name"))
            if k == "IfStmt":
                c = kids(n)
                g = guarded or bool(self.atoms_in(c[0], set()))
                go(c[0], guarded)
                for x in c[1:]:
                    go(x, g)
                return
            for x in kids(n):
                go(x, guarded)
        go(body, False)

        # poison encoded by omission: `if (<poison>) return; ...; flag = true;` leaves flag at its default for a poisoned value
        def omit(n):
            if n.get("kind") == "CompoundStmt":
                after = False
                for x in kids(n):
                    if after:
                        for m in walk(x):
                            if m.get("kind") == "BinaryOperator" and m.get("opcode") == "=":
                                l, r = kids(m)
                                l, r = strip(l), strip(r)
                                if l.get("kind") == "MemberExpr" and l.get("type", {}).get("qualType") in ("bool", "_Bool") and \
                                        (r.get("kind") == "CXXBoolLiteralExpr" or (r.get("kind") == "IntegerLiteral" and r.get("value") == "1")
                                         or (r.get("kind") == "DeclRefExpr" and ref_name(r)[0] == "true")):
                                    self.carriers.add(l.get("name"))
                    if x.get("kind") == "IfStmt" and self.atoms_in(kids(x)[0], set()) and any(y.get("kind") == "ReturnStmt" for y in walk(kids(x)[1])):
                        after = True
            for c in kids(n):
                omit(c)
        omit(body)

    # ---- atoms ----
    def atom_at(self, n, nullvars, logic):
        """the replacement that makes the atom at `n` say "not poison", or None if `n` is not an atom (n is already strip()ped)"""
        k = n.get("kind")
        if k == "BinaryOperator" and n.get("opcode") in ("==", "!="):
            a, b = kids(n)
            if self.is_root(a) or self.is_root(b):
                return "0" if n["opcode"] == "==" else "1"
            for v, o in ((a, b), (b, a)):
                if ref_name(v)[0] in nullvars and ref_name(v)[1] in ("VarDecl", "ParmVarDecl"):
                    if is_null_value(o):
                        return "0" if n["opcode"] == "==" else "1"
                    onm, okd = ref_name(o)
                    if okd == "EnumConstantDecl":  # the value 0 is the normal result; any other enumerator is the "could not" answer
                        normal = self.enumvals.get(onm, 0) == 0
                        return ("1" if normal else "0") if n["opcode"] == "==" else ("0" if normal else "1")
        if k == "CallExpr" and callee(n) in self.predicates:
            return "0"
        if k == "MemberExpr" and n.get("name") in self.carriers:
            return "0"
        if k == "UnaryOperator" and n.get("opcode") == "!":
            x = strip(kids(n)[0])
            if (x.get("kind") == "DeclRefExpr" and ref_name(x)[0] in nullvars) or (x.get("kind") == "CallExpr" and callee(x) in self.nullpoison):
                return "0"
        if k == "DeclRefExpr" and logic and ref_name(n)[1] in ("VarDecl", "ParmVarDecl") and ref_name(n)[0] in nullvars:
            return "1"
        return None

    def atoms_in(self, expr, nullvars):
        """[(node, replacement)]: the outermost atoms inside `expr`"""
        out = []

        def go(n, logic):
            m = strip(n)
            rep = self.atom_at(m, nullvars, logic)
            if rep is not None:
                out.append((m, rep))
                return
            sub = m.get("kind") == "BinaryOperator" and m.get("opcode") in ("&&", "||")
            for c in kids(m):
                go(c, sub)
        go(expr, True)
        return out

    def nullvars_of(self, body, fname=None):
        vs = set(self.poison_params.get(fname, ()))
        for n in walk(body):
            if n.get("kind") == "VarDecl" and kids(n) and any(callee(x) in self.nullpoison for x in walk(kids(n)[0]) if x.get("kind") == "CallExpr"):
                vs.add(n.get("name"))
            if n.get("kind") == "BinaryOperator" and n.get("opcode") == "=":
                l, r = kids(n)
                if ref_name(l)[0] and any(callee(x) in self.nullpoison for x in walk(r) if x.get("kind") == "CallExpr"):
                    vs.add(ref_name(l)[0])
        return vs

    # ---- the report ----
    def sinks(self):
        out = []
        self.flow_sites = []
        for name in sorted(self.funcs):
            f, fn = self.funcs[name]
            if name in self.predicates:
                continue
            body = next(c for c in kids(fn) if c.get("kind") == "CompoundStmt")
            nv = self.nullvars_of(body, name)
            groups, flow, owner = {}, {}, {}

            def visit(n, ctx, logic, dec):
                m = strip(n)
                if is_assert(m):
                    return
                rep = self.atom_at(m, nv, logic)
                if rep is not None:
                    if ctx is not None:
                        (groups if dec else flow).setdefault(id(ctx), (ctx, []))[1].append((m, rep))
                    return
                k = m.get("kind")
                c = kids(m)
                if k in ("IfStmt", "WhileStmt", "ConditionalOperator"):
                    owner[id(c[0])] = m
                    visit(c[0], c[0], True, True)
                    for x in c[1:]:
                        visit(x, x if k != "ConditionalOperator" else ctx, False, False)
                elif k == "ForStmt":
                    for x in c[:-1]:
                        owner[id(x)] = m
                        visit(x, x, True, True)
                    visit(c[-1], c[-1], False, False)
                elif k == "CompoundStmt":
                    for x in c:
                        visit(x, x, False, False)
                elif k == "BinaryOperator" and m.get("opcode") == "=":
                    lhs = strip(c[0])
                    if lhs.get("kind") == "MemberExpr" and lhs.get("name") in self.carriers and ctx is not None:
                        flow.setdefault(id(ctx), (ctx, []))[1].append((lhs, "0"))  # a write of a carrier: flow, not a decision
                    visit(c[1], ctx, False, False)  # the target of an assignment is a write, not a read
                else:
                    d = dec or (k == "BinaryOperator" and m.get("opcode") in ("&&", "||", "<", ">", "<=", ">=", "==", "!=")) or \
                        (k == "UnaryOperator" and m.get("opcode") == "!")
                    sub = k == "BinaryOperator" and m.get("opcode") in ("&&", "||")
                    for x in c:
                        visit(x, ctx, sub, d)
            visit(body, None, False, False)
            for key, atoms in flow.values():
                if "_s" in key:
                    self.flow_sites.append({"file": os.path.basename(key["_f"]), "function": name, "line": self.line_of(key), "text": self.text(key)[:100]})
            seen = {}
            for key, atoms in sorted(groups.values(), key=lambda g: g[0].get("_s", 0)):
                txt = self.text(key)
                if not txt or "_s" not in key:
                    continue
                h = hashlib.sha1(txt.encode()).hexdigest()[:8]
                idx = seen.get((name, h), 0)
                seen[(name, h)] = idx + 1
                line = self.line_of(key)
                own = owner.get(id(key), key)  # the whole if / while / for / ?: the condition decides; the statement itself otherwise
                out.append({
                    "id": "%s::%s::%s::%d" % (os.path.basename(key["_f"]), name, h, idx),
                    "file": os.path.basename(key["_f"]), "function": name, "line": line, "text": txt[:140],
                    "extent": [self.line_of(own), self.line_of(dict(own, _s=own["_e"] - 1))] if "_e" in own else [line, line],
                    "atoms": sorted(({"start": a["_s"], "end": a["_e"], "replace": rep, "text": self.text(a)[:100]} for a, rep in atoms if "_s" in a),
                                    key=lambda d: d["start"]),
                })
        return out

    def line_of(self, n):
        src = self.sources.get(n["_f"])
        return src[:n["_s"]].count(b"\n") + 1 if src else 0

    def function_extents(self):
        return [{"file": os.path.basename(f), "function": n, "start": self.line_of(fn), "end": self.line_of(dict(fn, _s=fn["_e"] - 1))}
                for n, (f, fn) in sorted(self.funcs.items()) if "_e" in fn]

    def predicate_defs(self):
        out = []
        for name in sorted(self.predicates):
            f, fn = self.funcs[name]
            out.append({"file": os.path.basename(f), "function": name, "line": self.line_of(fn) if "_s" in fn else 0, "end": self.line_of(dict(fn, _s=fn["_e"] - 1)) if "_e" in fn else 0})
        return out

    def emitters(self):
        out = []
        for name in sorted(self.funcs):
            f, fn = self.funcs[name]
            for n in walk(fn):
                if n.get("kind") == "CallExpr" and callee(n) == "ir_error" and "_s" in n:
                    code = next((callee(x) for x in walk(n) if x.get("kind") == "CallExpr" and (callee(x) or "").startswith("diag_ir_")), None)
                    out.append({"file": os.path.basename(n["_f"]), "function": name, "line": self.line_of(n), "constructor": code})
        return out

    def thin_producers(self):
        """the creation points of poison: functions that reference a poison enumerator directly, and the thin wrappers (at most three statements) around them or each other"""
        thin = {n for n, (f, fn) in self.funcs.items() if n in self.producers and any(self.is_root(x) and not self.root_compare_parent(fn, x)
                                                                                        for x in walk(fn) if x.get("kind") == "DeclRefExpr")}
        for _ in range(10):
            for n, (f, fn) in self.funcs.items():
                body = next(c for c in kids(fn) if c.get("kind") == "CompoundStmt")
                if n in self.producers and n not in thin and len(kids(body)) <= 3 and any(callee(x) in thin for x in walk(body) if x.get("kind") == "CallExpr"):
                    thin.add(n)
        return thin

    def producer_sites(self):
        out = []
        thin = self.thin_producers()
        for name in sorted(self.funcs):
            f, fn = self.funcs[name]
            for n in walk(fn):
                if n.get("kind") == "DeclRefExpr" and self.is_root(n) and not self.root_compare_parent(fn, n) and "_s" in n:
                    out.append({"file": os.path.basename(n["_f"]), "function": name, "line": self.line_of(n), "kind": "enumerator"})
                if n.get("kind") == "CallExpr" and callee(n) in thin and "_s" in n:
                    out.append({"file": os.path.basename(n["_f"]), "function": name, "line": self.line_of(n), "kind": "call " + callee(n)})
        return out

    def root_compare_parent(self, fn, target):
        return any(self.root_compare(x) and any(y is target for y in walk(x)) for x in walk(fn))

    def manifest(self):
        return {
            "version": 1,
            "files": sorted(set(f for f, _ in self.funcs.values())),
            "root": ROOT_ENUM, "aliases": sorted(self.aliases), "predicates": sorted(self.predicates), "producers": sorted(self.producers),
            "nullpoison": sorted(self.nullpoison), "reporters": sorted(self.reporters), "carriers": sorted(self.carriers),
            "emitters": self.emitters(), "producer_sites": self.producer_sites(), "sinks": self.sinks(), "flow": self.flow_sites,
            "predicate_defs": self.predicate_defs(), "functions": self.function_extents(),
        }


def stable(man):
    """the committed form of the manifest (tests/nocascade/sinks.json): identity only, nothing that moves when a line is added above (no line numbers, extents or byte
    offsets), so `--check` fails when the SET of sinks, emitters or producers changes and not on every edit of a builder file"""
    out = {k: man[k] for k in ("version", "files", "root", "aliases", "predicates", "producers", "nullpoison", "reporters", "carriers")}
    out["emitters"] = sorted({(e["file"], e["function"], e["constructor"] or "?") for e in man["emitters"]})
    out["emitters"] = [{"file": f, "function": fn, "constructor": c} for f, fn, c in out["emitters"]]
    out["producer_sites"] = sorted({(p["file"], p["function"], p["kind"]) for p in man["producer_sites"]})
    out["producer_sites"] = [{"file": f, "function": fn, "kind": k} for f, fn, k in out["producer_sites"]]
    out["sinks"] = [{"id": x["id"], "file": x["file"], "function": x["function"], "text": x["text"],
                     "atoms": [{"text": a["text"], "replace": a["replace"]} for a in x["atoms"]]} for x in man["sinks"]]
    return out


def builder_headers(root=None):
    d = os.path.join(root or ROOT, SRCDIR)
    return sorted(f for f in os.listdir(d) if f.endswith(".h") and is_builder(SRCDIR + "/" + f))


def load(root=None):
    root = root or ROOT
    files = [os.path.join(SRCDIR, f) for f in builder_files(root)]
    if not files:
        raise Fail("no builder files found under %s" % SRCDIR)
    with ThreadPoolExecutor(6) as ex:
        asts = list(ex.map(lambda p: ast_of(p, root), files))
    sources = {}
    for tu in asts:
        pass
    for dirpath, _, names in os.walk(os.path.join(root, SRCDIR)):
        for nm in names:
            if nm.endswith((".c", ".h")):
                sources[os.path.join(SRCDIR, nm)] = open(os.path.join(dirpath, nm), "rb").read()
    prog = Program(asts, sources, root)
    # the builder headers are analysed on purpose: every function with a body that a header defines is in `funcs`, and the headers are listed in the manifest
    # when they define one. A header function whose body the AST of EVERY translation unit lacks cannot exist (clang parsed the header), so the by-name check
    # in Program covers it; here we only make sure the headers were seen at all.
    for h in builder_headers(root):
        if SRCDIR + "/" + h not in sources:
            raise Fail("builder header %s was not read" % h)
    return prog


def audit_diff(man):
    """the 143-line audit table (tools/audit_ir_poison.py) against this tool: every audit line must be a line this tool also classifies; the tool's extra sinks are the under-count"""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import audit_ir_poison as A
    cands = A.candidates()
    mine = {}
    for s in man["sinks"]:
        for a in s["atoms"]:
            pass
        mine.setdefault((s["file"], s["line"]), "sink")
    for e in man["emitters"]:
        mine.setdefault((e["file"], e["line"]), "emitter")
    for p in man["producer_sites"]:
        mine.setdefault((p["file"], p["line"]), "producer")
    for p in man["flow"]:
        mine.setdefault((p["file"], p["line"]), "flow")
    for p in man["predicate_defs"]:
        for ln in range(p["line"], p["end"] + 1):
            mine.setdefault((p["file"], ln), "predicate")
    sink_lines = {}
    for s in man["sinks"]:
        sink_lines.setdefault(s["file"], []).append(s)
    missing = []
    for f, n, src, ln, col in cands:
        if (f, n) in mine:
            continue
        # a multi-line statement: the audit may point at a continuation line; accept any sink / emitter starting within 6 lines above
        if any((f, n - d) in mine for d in range(1, 7)):
            continue
        missing.append((f, n, src))
    return cands, missing


def main(argv):
    if "--check" in argv:
        path = argv[argv.index("--check") + 1]
        prog = load()
        now = json.dumps(stable(prog.manifest()), indent=1, sort_keys=True) + "\n"
        if not os.path.exists(path) or open(path).read() != now:
            print("poison_paths: %s is stale or missing: run python3 tools/poison_paths.py --json %s and commit the result" % (path, path))
            return 1
        print("poison_paths: %s is current (%d sinks)" % (path, len(json.loads(now)["sinks"])))
        return 0
    prog = load()
    man = prog.manifest()
    if "--json" in argv:
        out = argv[argv.index("--json") + 1]
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        open(out, "w").write(json.dumps(stable(man), indent=1, sort_keys=True) + "\n")
    print("files %d: %s" % (len(man["files"]), " ".join(man["files"])))
    for k in ("aliases", "predicates", "producers", "nullpoison", "carriers"):  # reporters are in the manifest
        print("%-11s %s" % (k, " ".join(man[k])))
    print("emitters %d (ir_error call sites), producer sites %d, sinks %d" % (len(man["emitters"]), len(man["producer_sites"]), len(man["sinks"])))
    for s in man["sinks"]:
        print("  sink %-12s %-38s %4d  %s" % (s["file"], s["function"], s["line"], s["text"][:70]))
    if "--audit-diff" in argv:
        cands, missing = audit_diff(man)
        print("\naudit_ir_poison.py: %d lines; %d of them are not classified by this tool:" % (len(cands), len(missing)))
        for f, n, src in missing:
            print("  NOT CLASSIFIED %s:%d  %s" % (f, n, src[:90]))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Fail as e:
        print("poison_paths: " + str(e), file=sys.stderr)
        sys.exit(2)
