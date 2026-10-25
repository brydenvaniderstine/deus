#!/usr/bin/env python3
# Fixed-seed random property test for the rename fix-its (typed-IR step 18; re-audit findings F2, F3, d1).
#
# Usage: tools/rename_property_test.py <ir_dump> [N=2500] [first_seed=0]      (make test-rename-property)
#
# Each program is random: a few top-level functions (well and badly named), and bodies of nested if / else if / else / while / for-let
# blocks with lets, assignments, bad assignment targets (a parenthesized name, a call), reads, calls, functions used as values and
# names that are declared nowhere; names are drawn from a small pool that deliberately contains a name and its snake_case target
# (`helperFn` / `helper_fn`, `itemCount` / `item_count`), so renames collide with locals, functions and unknown names.
# For each program: run `ir_dump`, apply every fix (`ir_dump --fixed`), re-check, and assert
#   1. the fixed program has no error kind the original lacked, and the same number of every error other than the naming errors
#      (a rename must not create or hide a duplicate, an unknown name, a function-as-value, a bad assignment target);
#   2. every identifier token resolves to the same declaration as before, and every declaration is bound or a duplicate exactly as before.
# Resolution is computed by an independent resolver written here (not the compiler's): the language has one flat function scope per
# block nest, no shadowing (a declaration of a name that is visible is a duplicate and binds nothing), calls resolve among the functions
# only, a name used as a value that is not a visible local is the function of that name or unknown.
# The fixed text keeps every identifier token in place (edits replace names only), so tokens are matched by position.
#
# Part two (step 19, re-audit 3 finding 10): programs with STRUCTS, FIELDS, constructions (named, positional, a field given twice or unknown),
# field accesses, type annotations (a struct, a function named in a type position, an unknown type) and unknown callees and fields. The
# resolver of part two (resolve2) is SPELLING based, because the stale spellings of finding 6 are exactly the names the checker resolves to
# nothing: a token resolves to the declaration whose WRITTEN name it equals, in its namespace (top level for types, callees and
# construction callees; the struct's fields for field accesses and construction argument names), or to nothing. A rename must keep every
# token's resolution, whether or not the checker resolved it. Part one's counts and part two's are compared with
# tests/rename_property.expected by `make test-rename-property`.
import random, re, subprocess, sys, os, tempfile

BIN = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 2500
SEED0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0

FNS = ["helperFn", "helper_fn", "doIt", "do_it", "fooBar", "foo_bar"]
LOCALS = ["ab", "aB", "Ab", "a_b", "helper_fn", "helperFn", "do_it", "doIt", "foo_bar", "fooBar", "itemCount", "item_count", "x", "X", "q1"]
UNKNOWN = ["itemCount", "item_count", "helper_fn", "helperFn", "zz"]
KEYWORDS = {"u16", "u32", "fn", "let", "if", "else", "while", "for", "return", "u8", "bool", "true", "false", "struct", "break", "continue", "len", "manna", "v0"}
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def gen(rng):
    fns = rng.sample(FNS, rng.randint(1, 4))
    lines = ["manna v0;"]
    for f in fns:
        lines.append(f"fn {f}() -> u8 {{")
        lines.append("    return 1;")
        lines.append("}")
    nfun = rng.randint(1, 2)
    for k in range(nfun):
        params = ["c: bool"] + ([f"{rng.choice(LOCALS)}: u8"] if rng.random() < 0.4 else [])
        lines.append(f"fn body{k}({', '.join(params)}) -> u8 {{")
        names_in_scope = [[p.split(":")[0] for p in params]]

        def use_name():
            vis = [n for sc in names_in_scope for n in sc]
            r = rng.random()
            if vis and r < 0.55:
                return rng.choice(vis)
            if r < 0.8:
                return rng.choice(fns)
            return rng.choice(UNKNOWN)

        def use_expr():
            n = use_name()
            f = rng.random()
            if f < 0.5:
                return n
            if f < 0.7:
                return f"({n})"
            if f < 0.8:
                return f"( ( {n} ) )"
            return f"{rng.choice(fns)}()"

        def block(depth, ind):
            for _ in range(rng.randint(1, 4)):
                k = rng.random()
                pad = " " * ind
                if k < 0.35:
                    nm = rng.choice(LOCALS)
                    lines.append(f"{pad}let {nm}: u8 = {use_expr()};")
                    names_in_scope[-1].append(nm)
                elif k < 0.55:
                    t = rng.random()
                    tgt = use_expr() if t < 0.7 else f"{rng.choice(fns)}()"
                    lines.append(f"{pad}{tgt} = 2;")
                elif k < 0.65:
                    lines.append(f"{pad}{use_expr()};")
                elif depth < 3 and k < 0.8:
                    lines.append(f"{pad}if c {{")
                    names_in_scope.append([]); block(depth + 1, ind + 4); names_in_scope.pop()
                    while rng.random() < 0.3:
                        lines.append(f"{pad}}} else if c {{")
                        names_in_scope.append([]); block(depth + 1, ind + 4); names_in_scope.pop()
                    if rng.random() < 0.5:
                        lines.append(f"{pad}}} else {{")
                        names_in_scope.append([]); block(depth + 1, ind + 4); names_in_scope.pop()
                    lines.append(f"{pad}}}")
                elif depth < 3 and k < 0.88:
                    lines.append(f"{pad}while c {{")
                    names_in_scope.append([]); block(depth + 1, ind + 4); names_in_scope.pop()
                    lines.append(f"{pad}}}")
                elif depth < 3:
                    nm = rng.choice(LOCALS)
                    lines.append(f"{pad}for let {nm}: u8 = 0; {nm} < 3; {nm} = {nm} + 1 {{")
                    names_in_scope.append([nm]); block(depth + 1, ind + 4); names_in_scope.pop()
                    lines.append(f"{pad}}}")
        block(0, 4)
        lines.append("    return 0;")
        lines.append("}")
    return "\n".join(lines) + "\n"


def resolve(src):
    """For each identifier token (in order): ('decl', bound|dup:<first token>) | ('local', decl token) | ('fn', decl token) | ('unknown',)."""
    lines = src.split("\n")
    tokens = []  # (line index, start col, text) of every identifier that is not a keyword
    per_line = []
    for li, line in enumerate(lines):
        toks = [(m.start(), m.group()) for m in IDENT.finditer(line) if m.group() not in KEYWORDS]
        per_line.append(toks)
    # token numbering
    idx = {}
    n = 0
    for li, toks in enumerate(per_line):
        for (c, t) in toks:
            idx[(li, c)] = n
            n += 1
    fn_decl = {}
    for li, line in enumerate(lines):
        m = re.match(r"fn (\w+)\(", line)
        if m:
            fn_decl.setdefault(m.group(1), idx[(li, m.start(1))])
    out = [None] * n
    scopes = []  # list of dict name -> declaring token

    def lookup(name):
        for sc in reversed(scopes):
            if name in sc:
                return sc[name]
        return None

    def declare(li, c, name):
        t = idx[(li, c)]
        first = lookup(name)
        if first is None:
            scopes[-1][name] = t
            out[t] = ("decl", "bound")
        else:
            out[t] = ("decl", "dup", first)

    def use(li, c, name, line):
        t = idx[(li, c)]
        if re.match(r"\s*\(", line[c + len(name):]):  # a call: functions only
            out[t] = ("fn", fn_decl[name]) if name in fn_decl else ("unknown",)
            return
        d = lookup(name)
        if d is not None:
            out[t] = ("local", d)
        elif name in fn_decl:
            out[t] = ("fn", fn_decl[name])
        else:
            out[t] = ("unknown",)

    for li, line in enumerate(lines):
        toks = per_line[li]
        s = line.strip()
        m = re.match(r"fn (\w+)\((.*)\) -> u8 \{$", s)
        if m:
            if m.group(1) in fn_decl and idx[(li, line.index(m.group(1)))] != fn_decl[m.group(1)]:
                pass
            out[idx[(li, line.index("fn ") + 3)]] = ("fn-decl",)
            scopes.append({})
            for (c, t) in toks[1:]:
                declare(li, c, t)
            continue
        if s == "}":
            scopes.pop()
            continue
        if re.match(r"\} else", s):
            scopes.pop()
            for (c, t) in toks:
                use(li, c, t, line)
            scopes.append({})
            continue
        m = re.match(r"for let (\w+): u8 = 0; ", s)
        if m:
            scopes.append({})
            declare(li, toks[0][0], toks[0][1])
            for (c, t) in toks[1:]:
                use(li, c, t, line)
            continue
        m = re.match(r"let (\w+): u8 = ", s)
        if m:
            for (c, t) in toks[1:]:
                use(li, c, t, line)
            declare(li, toks[0][0], toks[0][1])
            continue
        if s.endswith("{"):  # if / while
            for (c, t) in toks:
                use(li, c, t, line)
            scopes.append({})
            continue
        for (c, t) in toks:
            use(li, c, t, line)
    return out


def run(args):
    r = subprocess.run([BIN] + args, capture_output=True, text=True)
    assert r.returncode == 0 and not r.stderr, (args, r.returncode, r.stderr[:500])
    return r.stdout


def codes_of(out):
    return [m.group(1) for m in re.finditer(r"^\d+:\d+ (E\d{4}) ", out, re.M)]



# ---------------------------------------------------------------- part two: structs, fields, constructions, types

TOP_STRUCTS = ["Pt", "pt", "myPt", "MyPt", "Box", "boxItem", "My_Box"]
TOP_FNS = ["pt", "myPt", "helperFn", "helper_fn", "doIt", "do_it", "Box2"]
FIELD_POOL = ["xs", "xS", "x_s", "yVal", "y_val", "zzOne", "zz_one", "zz1"]
LOCAL_POOL = ["lA", "lB", "l_b", "lCount", "l_count", "itemOne", "item_one", "q9"]
UNKNOWN_TYPES = ["Zed", "zedType", "Pt2"]
UNKNOWN_FNS = ["zedFn", "zed_fn", "helperFn2"]


def gen2(rng):
    top = rng.sample(TOP_STRUCTS, rng.randint(1, 3)) + rng.sample(TOP_FNS, rng.randint(1, 3))
    rng.shuffle(top)
    structs, fns = [], []   # (name, [fields]); function names
    fields_of = {}
    sig_errors = rng.random() < 0.3   # a function or an unknown type in a signature or field is a declaration error: it stops the build before the bodies
    lines = ["manna v0;"]
    for name in top:
        if name in TOP_STRUCTS and name not in fns:
            fl = rng.sample(FIELD_POOL, rng.randint(1, 3))
            if sig_errors and rng.random() < 0.3:
                fl.append(fl[0])  # a field given twice in the declaration
            lines.append(f"struct {name} {{")
            for f in fl:
                r = rng.random()
                ty = "u8"
                if r < 0.25 and structs:
                    ty = rng.choice(structs)
                elif sig_errors and r < 0.45 and fns:
                    ty = rng.choice(fns)          # a function named in a type position
                elif sig_errors and r < 0.6:
                    ty = rng.choice(UNKNOWN_TYPES)
                lines.append(f"    {f}: {ty},")
            lines.append("}")
            if name not in fields_of:
                fields_of[name] = fl
            structs.append(name)
        else:
            fns.append(name)
            lines.append(f"fn {name}() -> u8 {{")
            lines.append("    return 1;")
            lines.append("}")
    for k in range(rng.randint(1, 2)):
        var_ty = {}   # local/param -> its annotation text
        params = []
        for pn in rng.sample(LOCAL_POOL, rng.randint(0, 2)):
            ty = rng.choice(structs + ["u8"] + (fns[:1] if sig_errors and rng.random() < 0.4 else []) + (UNKNOWN_TYPES[:1] if sig_errors and rng.random() < 0.4 else []))
            params.append(f"{pn}: {ty}")
            var_ty[pn] = ty
        lines.append(f"fn body{k}({', '.join(params)}) -> u8 {{")
        free = [n for n in LOCAL_POOL if n not in var_ty]
        rng.shuffle(free)

        def struct_vars():
            return [v for v, t in var_ty.items() if t in fields_of]

        for _ in range(rng.randint(3, 7)):
            r = rng.random()
            if r < 0.30 and structs and free:
                s_name = rng.choice(structs)
                fl = fields_of[s_name]
                args = []
                for f in fl:
                    q = rng.random()
                    if q < 0.12:
                        args.append("1")                                   # positional: E0240
                    elif q < 0.22:
                        args.append(f"{rng.choice(FIELD_POOL)}: 1")        # maybe an unknown field name (or the TARGET spelling of a rename)
                    elif q < 0.28:
                        args.append(f"{f}: 1")
                        args.append(f"{f}: 2")                             # given twice
                    else:
                        args.append(f"{f}: 1")
                v = free.pop()
                if rng.random() < 0.5:
                    lines.append(f"    let {v}: {s_name} = {s_name}({', '.join(args)});")
                else:
                    lines.append(f"    let {v} = {s_name}({', '.join(args)});")
                var_ty[v] = s_name
            elif r < 0.55 and struct_vars() and free:
                v = rng.choice(struct_vars())
                s_name = var_ty[v]
                f = rng.choice(fields_of[s_name]) if rng.random() < 0.7 else rng.choice(FIELD_POOL)
                n = free.pop()
                lines.append(f"    let {n}: u8 = {v}.{f};")
                var_ty[n] = "u8"
            elif r < 0.65 and struct_vars():
                v = rng.choice(struct_vars())
                lines.append(f"    {v}.{rng.choice(FIELD_POOL)};")
            elif r < 0.75 and free:
                n = free.pop()
                ty = rng.choice(structs + fns + UNKNOWN_TYPES + TOP_STRUCTS)
                lines.append(f"    let {n}: {ty} = 1;")
                var_ty[n] = ty
            elif r < 0.90:
                lines.append(f"    {rng.choice(fns + UNKNOWN_FNS + structs)}();")
            elif free:
                n = free.pop()
                lines.append(f"    let {n}: u8 = {rng.choice(fns + UNKNOWN_FNS)}();")
                var_ty[n] = "u8"
        lines.append("    return 0;")
        lines.append("}")
    return "\n".join(lines) + "\n"


def resolve2(src):
    """Spelling-based resolution of every identifier token of a part-two program (see the header). Returns one tuple per token."""
    lines = src.split("\n")
    per_line = [[(m.start(), m.group()) for m in IDENT.finditer(l) if m.group() not in KEYWORDS] for l in lines]
    idx, n = {}, 0
    for li, toks in enumerate(per_line):
        for c, t in toks:
            idx[(li, c)] = n
            n += 1
    top, fields = {}, {}                  # written name -> first declaring token; struct name -> {field name -> first declaring token}
    table = None                          # the field table of the struct being read (None inside a duplicate declaration)
    for li, l in enumerate(lines):
        m = re.match(r"struct (\w+) \{$", l)
        if m:
            t = idx[(li, m.start(1))]
            top.setdefault(m.group(1), t)
            table = fields.setdefault(m.group(1), {}) if top[m.group(1)] == t else None
            continue
        m = re.match(r"    (\w+): (\w+),$", l)
        if m and table is not None:
            table.setdefault(m.group(1), idx[(li, m.start(1))])
            continue
        m = re.match(r"fn (\w+)\(", l)
        if m:
            top.setdefault(m.group(1), idx[(li, m.start(1))])
    out = [None] * n
    local = {}
    var_struct = {}
    struct_tok = {nm: top[nm] for nm in fields}    # a name that is both a function and a struct is the one declared first

    def top_res(name):
        return ("top", top[name]) if name in top else ("top-none",)

    def _is_struct(name):
        return name in fields and top[name] == struct_tok[name]

    cur = None
    for li, l in enumerate(lines):
        toks = per_line[li]
        T = lambda k: idx[(li, toks[k][0])]
        m = re.match(r"struct (\w+) \{$", l)
        if m:
            nm = m.group(1)
            out[T(0)] = ("sdecl", "bound") if top[nm] == T(0) else ("sdecl", "dup", top[nm])
            cur = nm if top[nm] == T(0) else None
            continue
        m = re.match(r"    (\w+): (\w+),$", l)
        if m:
            out[T(0)] = ("fdecl", "bound") if cur is not None and fields[cur].get(m.group(1)) == T(0) else ("fdecl", "dup")
            if len(toks) > 1:
                out[T(1)] = top_res(m.group(2))
            continue
        if l == "}":
            cur = None
            continue
        m = re.match(r"fn (\w+)\((.*)\) -> u8 \{$", l)
        if m:
            local, var_struct = {}, {}
            out[T(0)] = ("fn", "bound") if top[m.group(1)] == T(0) else ("fn", "dup", top[m.group(1)])
            k = 1
            for p in [x for x in m.group(2).split(", ") if x]:
                pn, pt = p.split(": ")
                out[T(k)] = ("ldecl",)
                local[pn] = T(k)
                k += 1
                if pt in KEYWORDS:
                    var_struct[pn] = None
                else:
                    out[T(k)] = top_res(pt)
                    var_struct[pn] = pt if pt in fields and _is_struct(pt) else None
                    k += 1
            continue
        m = re.match(r"    let (\w+)(?:: (\w+))? = (.*);$", l)
        if m:
            k = 0
            name, ann, rhs = m.group(1), m.group(2), m.group(3)
            out[T(0)] = ("ldecl",)
            local[name] = T(0)
            k = 1
            if ann and ann not in KEYWORDS:
                out[T(k)] = top_res(ann)
                k += 1
            vs = None
            mc = re.match(r"(\w+)\((.*)\)$", rhs)
            mf = re.match(r"(\w+)\.(\w+)$", rhs)
            if mc:
                callee = mc.group(1)
                out[T(k)] = top_res(callee)
                k += 1
                is_s = callee in fields and _is_struct(callee)
                for a in [x for x in mc.group(2).split(", ") if x]:
                    ma = re.match(r"(\w+): \d+$", a)
                    if ma:
                        out[T(k)] = ("fld", top[callee], fields[callee].get(ma.group(1))) if is_s else ("fld-nostruct",)
                        k += 1
                if is_s:
                    vs = callee
            elif mf:
                out[T(k)] = ("local", local[mf.group(1)])
                k += 1
                st = var_struct.get(mf.group(1))
                out[T(k)] = ("fld", top[st], fields[st].get(mf.group(2))) if st else ("fld-nostruct",)
                k += 1
            if ann and ann not in KEYWORDS:
                vs = ann if ann in fields and _is_struct(ann) else None
            elif ann:
                vs = None
            var_struct[name] = vs
            continue
        m = re.match(r"    (\w+)\.(\w+);$", l)
        if m:
            out[T(0)] = ("local", local[m.group(1)])
            st = var_struct.get(m.group(1))
            out[T(1)] = ("fld", top[st], fields[st].get(m.group(2))) if st else ("fld-nostruct",)
            continue
        m = re.match(r"    (\w+)\(\);$", l)
        if m:
            out[T(0)] = top_res(m.group(1))
            continue
    assert all(o is not None for o in out), [(i, per_line) for i, o in enumerate(out) if o is None][:1]
    return out


def check_part(label, n, seed0, gen_fn, resolve_fn, watch=()):
    tmp = tempfile.mkdtemp()
    p, q = os.path.join(tmp, "in.mn"), os.path.join(tmp, "fixed.mn")
    stats = dict(programs=0, with_naming_errors=0, changed=0, tokens_renamed=0, unchanged_blocked=0)
    for w in watch:
        stats[w] = 0
    for it in range(n):
        seed = seed0 + it
        src = gen_fn(random.Random(seed))
        open(p, "w").write(src)
        out = run([p])
        codes = codes_of(out)
        stats["programs"] += 1
        for w in watch:
            stats[w] += w in codes   # programs that contain this error, so the report shows the interesting shapes were generated
        naming = [c for c in codes if c in ("E0280", "E0281")]
        if not naming:
            continue
        stats["with_naming_errors"] += 1
        fixed = run(["--fixed", p])
        open(q, "w").write(fixed)
        if fixed != src:
            stats["changed"] += 1
            stats["tokens_renamed"] += sum(1 for a, b in zip(IDENT.findall(src), IDENT.findall(fixed)) if a != b)
        else:
            stats["unchanged_blocked"] += 1
        out2 = run([q])
        codes2 = codes_of(out2)
        problems = []
        if len(IDENT.findall(src)) != len(IDENT.findall(fixed)):
            problems.append("the fixed source has a different number of identifier tokens")
        else:
            others = lambda cs: sorted(c for c in cs if c not in ("E0280", "E0281"))
            if others(codes2) != others(codes):
                problems.append(f"errors other than naming changed: {others(codes)} -> {others(codes2)}")
            if len([c for c in codes2 if c in ("E0280", "E0281")]) > len(naming):
                problems.append("more naming errors after the fix")
            r1, r2 = resolve_fn(src), resolve_fn(fixed)
            if r1 != r2:
                diffs = [i for i in range(len(r1)) if r1[i] != r2[i]]
                problems.append(f"{len(diffs)} identifier(s) resolve differently, first: token {diffs[0]} {r1[diffs[0]]} -> {r2[diffs[0]]}")
        if problems:
            print(f"=== {label} seed {seed}: " + "; ".join(problems))
            print(src)
            print("--- fixed")
            print(fixed)
            print("--- original diagnostics:\n" + out + "--- fixed diagnostics:\n" + out2)
            sys.exit(1)
    return stats


def main():
    st = check_part("part one", N, SEED0, gen, resolve)
    print("rename property test: %(programs)d random programs, %(with_naming_errors)d with naming errors, fixes changed %(changed)d "
          "(%(tokens_renamed)d identifiers renamed), %(unchanged_blocked)d left unchanged; no new error, no use resolves differently" % st)
    assert st["changed"] > N // 10, "the generator no longer exercises the fixes"
    WATCH = ("E0220", "E0232", "E0240", "E0242", "E0244", "E0210")
    st2 = check_part("part two", N, SEED0, gen2, resolve2, WATCH)
    print("rename property test, structs/fields/types: %(programs)d random programs, %(with_naming_errors)d with naming errors, fixes changed %(changed)d "
          "(%(tokens_renamed)d identifiers renamed), %(unchanged_blocked)d left unchanged; no new error, every spelling resolves as before" % st2)
    print("  programs with E0220 (unknown type) %(E0220)d, E0232 (unknown callee) %(E0232)d, E0240 (positional construction) %(E0240)d, "
          "E0242 (unknown field) %(E0242)d, E0244 (field given twice) %(E0244)d, E0210 (duplicate top-level) %(E0210)d" % st2)
    if N >= 1000:
        assert all(st2[w] > 0 for w in WATCH), "the part-two generator no longer produces every shape: %s" % {w: st2[w] for w in WATCH}
    assert st2["changed"] > N // 10, "the part-two generator no longer exercises the fixes"


main()
