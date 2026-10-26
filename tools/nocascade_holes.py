#!/usr/bin/env python3
"""The consumer HOLES of the typed IR, derived from tools/mannac/ast.h (typed-IR step 21f; the step 21 plan, "Golden generator").

A hole is a place in a program where a poisoned value can sit and be consumed: a struct member of the AST whose static type is `Expr *` or `Type *`, and every
element of a list of expressions (a NodeList whose element comment says `Expr*`). The tool reads ast.h through clang's JSON AST, so a new AST child slot appears
here with no edit to this file; `tools/gen_ir_nocascade.py` has a skeleton program per hole, and `--check` fails if a hole has none or a skeleton names a hole that
no longer exists (rule F5). Hole ids: `Record.member.field`, with `[]` after a list (`Expr.call.args[]`, `Stmt.let_stmt.init`, `Field.type`).

  python3 tools/nocascade_holes.py              print the holes
  python3 tools/nocascade_holes.py --check      compare with the skeletons of gen_ir_nocascade.py (exit 1 on any difference)
A hard failure (exit 2), never a skip: clang missing, ast.h not parsing, a NodeList whose element type the header does not say."""
import json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = "tools/mannac/ast.h"
POINTERS = {"Expr *": "Expr", "Type *": "Type"}   # the static types a poisoned value can occupy; Stmt * holds statements, not values


class Fail(Exception):
    pass


def ast(root=None):
    root = root or ROOT
    r = subprocess.run(["clang", "-std=c11", "-fsyntax-only", "-Xclang", "-ast-dump=json", "-x", "c", HEADER], capture_output=True, text=True, cwd=root)
    if r.returncode != 0:
        raise Fail("clang failed on %s:\n%s" % (HEADER, r.stderr[:1500]))
    return json.loads(r.stdout)


def element_comment(src, offset):
    """the element type a NodeList field declares in the comment on its own line (`/* args: Expr*`, `/* Field* */`)"""
    start = src.rfind("\n", 0, offset) + 1
    end = src.find("\n", offset)
    m = re.search(r"/\*[^*]*?\b(Expr|Stmt|Field|Param|Attribute|Type)\s*\*", src[start:end])
    return m.group(1) if m else None


def records(tu):
    """[(record name, [(member, [(field, qualType, line)])])]: the struct's direct fields as member '', and each anonymous-union struct as its member name"""
    out = []

    def fields_of(rec):
        fs = []
        for f in rec.get("inner", []):
            if f.get("kind") == "FieldDecl":
                fs.append((f.get("name"), f.get("type", {}).get("qualType", ""), f.get("loc", {}).get("offset")))   # clang prints `line` only when it changed: use the offset
        return fs

    def walk(rec, name):
        direct = fields_of(rec)
        members = [("", direct)]
        last = None
        for n in rec.get("inner", []):
            if n.get("kind") == "RecordDecl" and not n.get("name") and n.get("tagUsed") == "union":
                prev = None
                for m in n.get("inner", []):
                    if m.get("kind") == "RecordDecl" and not m.get("name"):
                        prev = m
                    elif m.get("kind") == "FieldDecl" and prev is not None:
                        members.append((m["name"], fields_of(prev)))
                        prev = None
        out.append((name, members))

    for n in tu.get("inner", []):
        if n.get("kind") == "RecordDecl" and n.get("name") and "completeDefinition" in n:
            walk(n, n["name"])
        if n.get("kind") == "TypedefDecl":
            for c in n.get("inner", []):
                tag = c.get("ownedTagDecl")
                if tag and tag.get("kind") == "RecordDecl":
                    pass
    # `typedef struct { ... } Field;` is an anonymous record owned by a typedef
    ids = {n["id"]: n for n in tu.get("inner", []) if "id" in n}
    for n in tu.get("inner", []):
        if n.get("kind") == "TypedefDecl":
            for c in n.get("inner", []):
                tag = c.get("ownedTagDecl")
                if tag and tag.get("id") in ids and ids[tag["id"]].get("kind") == "RecordDecl" and not ids[tag["id"]].get("name"):
                    walk(ids[tag["id"]], n["name"])
    return out


def derive(root=None):
    root = root or ROOT
    src = open(os.path.join(root, HEADER), "rb").read().decode("utf-8")  # offsets are bytes; ast.h is ASCII
    holes = set()
    for rec, members in records(ast(root)):
        for member, fields in members:
            for field, qt, off in fields:
                ident = ".".join(p for p in (rec, member, field) if p)
                if qt in POINTERS:
                    holes.add(ident)
                elif qt == "NodeList":
                    el = element_comment(src, off) if off is not None else None
                    if el is None:
                        raise Fail("%s: the NodeList %s does not say its element type in a comment on its line" % (HEADER, ident))
                    if el == "Expr":
                        holes.add(ident + "[]")
    return sorted(holes)


def main(argv):
    holes = derive()
    if "--check" in argv:
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import gen_ir_nocascade as gen
        have = set(gen.skeleton_holes())
        want = set(holes) - set(gen.EXCLUDED_HOLES)
        missing = sorted(want - have)
        orphan = sorted(have - set(holes))
        for h in missing:
            print("F5: hole %s has no skeleton in tools/gen_ir_nocascade.py" % h)
        for h in orphan:
            print("F5: skeleton %s names a hole that ast.h no longer has" % h)
        stale = sorted(set(gen.EXCLUDED_HOLES) - set(holes))
        for h in stale:
            print("F5: EXCLUDED_HOLES lists %s, which ast.h no longer has" % h)
        if missing or orphan or stale:
            return 1
        print("nocascade_holes: ok (%d holes in %s; %d with skeletons, %d excluded with a reason)" % (len(holes), HEADER, len(have), len(gen.EXCLUDED_HOLES)))
        return 0
    for h in holes:
        print(h)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Fail as e:
        print("nocascade_holes: " + str(e), file=sys.stderr)
        sys.exit(2)
