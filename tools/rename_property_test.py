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
import random, re, subprocess, sys, os, tempfile

BIN = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 2500
SEED0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0

FNS = ["helperFn", "helper_fn", "doIt", "do_it", "fooBar", "foo_bar"]
LOCALS = ["ab", "aB", "Ab", "a_b", "helper_fn", "helperFn", "do_it", "doIt", "foo_bar", "fooBar", "itemCount", "item_count", "x", "X", "q1"]
UNKNOWN = ["itemCount", "item_count", "helper_fn", "helperFn", "zz"]
KEYWORDS = {"fn", "let", "if", "else", "while", "for", "return", "u8", "bool", "true", "false", "struct", "break", "continue", "len", "manna", "v0"}
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


def main():
    tmp = tempfile.mkdtemp()
    p, q = os.path.join(tmp, "in.mn"), os.path.join(tmp, "fixed.mn")
    stats = dict(programs=0, with_naming_errors=0, changed=0, tokens_renamed=0, unchanged_blocked=0)
    for it in range(N):
        seed = SEED0 + it
        src = gen(random.Random(seed))
        open(p, "w").write(src)
        out = run([p])
        codes = codes_of(out)
        stats["programs"] += 1
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
            r1, r2 = resolve(src), resolve(fixed)
            if r1 != r2:
                diffs = [i for i in range(len(r1)) if r1[i] != r2[i]]
                problems.append(f"{len(diffs)} identifier(s) resolve differently, first: token {diffs[0]} {r1[diffs[0]]} -> {r2[diffs[0]]}")
        if problems:
            print(f"=== seed {seed}: " + "; ".join(problems))
            print(src)
            print("--- fixed")
            print(fixed)
            print("--- original diagnostics:\n" + out + "--- fixed diagnostics:\n" + out2)
            sys.exit(1)
    print("rename property test: %(programs)d random programs, %(with_naming_errors)d with naming errors, fixes changed %(changed)d "
          "(%(tokens_renamed)d identifiers renamed), %(unchanged_blocked)d left unchanged; no new error, no use resolves differently" % stats)
    assert stats["changed"] > N // 10, "the generator no longer exercises the fixes"


main()
