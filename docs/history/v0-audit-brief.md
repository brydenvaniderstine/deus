# v0 parser audit brief (fresh eyes on the #4 step)

Written at the close of the #4 parser step, for an external, independent audit before the tag `m2-parser-v0`. Same shape as the r1 kit: a 7-file pack, an auditor prompt, a reproduce step. Archived working record, not a source of truth: if this and `docs/M2_PLAN.md` / `docs/STATUS.md` disagree, those win.

## 1. What is being audited

The parser code that changed between the tag `m2-parser-r1` and `main` (`git diff m2-parser-r1 HEAD -- tools/mannac`: 14 files, +1012 / -156). That range holds two kinds of change, and the audit should treat both as unreviewed:

1. **Post-tag fixes** to regressions that `m2-parser-r1` shipped and an earlier external audit found: `cad102b` (all per-parse state resets at entry), `3553b67` (header recovery also consumes a following `else`/`else if`), `e3d6652` (`sync_skip`'s `(`/`[` depth masks only `;`), `c1daf01`/`f207107` (test leak, pins).
2. **The #4 step** (v0 grammar completion), as nine commits: step 0 `344d90c` (E0102 `expected` field), 1 `851ed50` (reserve `u8..i64`, `bool`, `unit`), 2 `34de06a` (conversions `u64(x)`), 3 `e95b26c` (array literals, `[v; N]`, the `'['` mode peek), 4 `d5b8800` (named args `Point(x: 1)`), 5 `97c2ef9` (one `relational` level, E0108, two-edit fix-it), 6 `7ebc8dc` (E0109 `-> unit`, the `(return_type)` dump), and two harness fixes `2b8b150`/`db4f712` (expression and block mode report leftover tokens).

Out of scope: the lexer beyond the ten new keywords, the arena, anything under `kernel/`, and every IR-stage check (struct-construction field rules, array/conversion typing, `bool`<->integer rejection, lvalue validity): those do not exist yet and are listed as to-do in M2_PLAN.md. A finding that something *semantic* is not rejected is not a parser bug.

Reported state at the time of writing (all from running the commands, 2026-10-10): 346 parser goldens, 15 lexer goldens, 12 `parser_fixes_test` cases, determinism 361/361, plain and ASan all green, coverage 99.69% with 7 uncovered lines (each justified in `docs/STATUS.md`), CI green on every pushed commit (last run 38083112051), libFuzzer parser 600 s / 469,740 execs and lexer 180 s / 2,545,004 execs clean with the last-resort backstop never reached. **Treat every one of those claims as something to disprove.**

## 2. The pack (exactly 7 files)

| File | Contents |
|---|---|
| `step4.diff` | `git diff m2-parser-r1 HEAD -- tools/mannac` (parser code only) |
| `code_bundle.txt` | all 20 files of `tools/mannac/` at HEAD, each under `===== FILE: tools/mannac/<name> =====` |
| `tests_bundle.txt` | every file under `tests/` added or changed since `m2-parser-r1` (358 files), plus the `.mn` input for each `.ast`/`.tokens`, same header format; a header ending `(no trailing newline)` marks a file whose final newline the bundler added |
| `M2_PLAN_s4.md` | `docs/M2_PLAN.md` section 4 (the grammar and the design record for this step) |
| `error_codes.md` | `docs/error_codes.md` |
| `tests_parser_list.txt` | `ls tests/parser` (all goldens, including the unchanged ones not bundled) |
| `BUILD.txt` | split, build and run commands, with the verification that was done |

### Regenerating the pack (maintainer step)

From a clean checkout of the commit under audit, with `D` an empty directory:

```bash
D=~/Desktop/v0-audit; mkdir -p $D
git diff m2-parser-r1 HEAD -- tools/mannac > $D/step4.diff
python3 - <<'PY'
import os,subprocess
D=os.path.expanduser('~/Desktop/v0-audit')
def bundle(paths,out):
    with open(out,'wb') as f:
        for p in paths:
            b=open(p,'rb').read(); nl=b.endswith(b'\n') or not b
            f.write(("===== FILE: %s%s =====\n"%(p,"" if nl else " (no trailing newline)")).encode()+b+(b"" if nl else b"\n"))
bundle(sorted('tools/mannac/'+x for x in os.listdir('tools/mannac')), D+'/code_bundle.txt')
ch={l.split('\t')[1] for l in subprocess.check_output(['git','diff','--no-renames','--name-status','m2-parser-r1','HEAD','--','tests']).decode().strip().split('\n') if l}
ch |= {f[:-4]+'.mn' for f in ch if f.endswith('.ast')} | {f[:-7]+'.mn' for f in ch if f.endswith('.tokens')}
bundle(sorted(x for x in ch if os.path.exists(x)), D+'/tests_bundle.txt')
PY
ls tests/parser > $D/tests_parser_list.txt
cp docs/error_codes.md $D/
awk '/^## 4\. Parser grammar/{p=1} /^## 5\. Test strategy/{p=0} p' docs/M2_PLAN.md > $D/M2_PLAN_s4.md
# BUILD.txt: see the copy kept with the pack (split + build + run, verified byte-exact)
```

(Recipe hardening, made when the final pack was regenerated after the audit fixes: the changed-tests list now uses `--no-renames`. With rename detection a renamed golden is listed under its OLD name only, which no longer exists, so its new name - and its input - can silently drop out of `tests_bundle.txt`. For this exact range (`m2-parser-r1..HEAD`) the result is identical either way, 396 files, because the renamed goldens' `.ast` files were rewritten in #4 step 5 and so appear as plain additions, which pull in their `.mn`; the fixes-only pack range (`581e4d4..HEAD`) is where it mattered. The counts in this brief (20 files, 358 test files, 346 goldens, run 38075342100) describe the pack as first built; the regenerated pack's `BUILD.txt` carries its own.)

## 3. Reproduce step (auditor)

Follow `BUILD.txt`: split the two bundles into a work directory, build `parser_dump` (plain) and `parser_dump-asan` with the exact flags there, and run any input with `./parser_dump file.mn`. Every golden is `file.mn` + `file.ast`; the check is `output == .ast`. A finding **must** be reproducible with these two binaries (and, where it concerns the unit tests, the commands in `BUILD.txt`) from the pack alone.

## 4. Auditor prompt

This block is the whole prompt and the only thing pasted to the reviewer. `~/Desktop/v0-audit-prompt.txt` is this block with the leading `> ` removed (generated, not hand-edited; the pack folder holds only the 7 files above).

> FIRST LINE OF YOUR ANSWER: could you build and run the pack? yes/no, with the build output if no. If no, still audit by reading the code and tests: mark every finding SUSPECTED and give your predicted output for it.
>
> You are auditing a hand-written recursive-descent parser for a small systems language (Manna v0), written in C. You have a pack of 7 files (below). The author's claims in this prompt were produced by the people who wrote the code; assume any of them can be wrong. Do not trust a golden test because it exists or passes: this project has already shipped a regression whose golden was re-baselined to match it. Your job is to find inputs for which the parser does the wrong thing, and to prove each one with a real run.
>
> **What is being audited.** The parser code that changed between the tag `m2-parser-r1` and `main`: `git diff m2-parser-r1 HEAD -- tools/mannac`, 14 files, +1012 / -156 (this is `step4.diff`). Treat all of it as unreviewed. It holds two kinds of change:
> 1. Post-tag fixes to regressions that `m2-parser-r1` shipped and an earlier external audit found: `cad102b` (all per-parse state resets at entry), `3553b67` (header recovery also consumes a following `else`/`else if`), `e3d6652` (`sync_skip`'s `(`/`[` depth masks only `;`), `c1daf01`/`f207107` (test leak, pins).
> 2. The step that completed the v0 grammar, as nine commits: `344d90c` (E0102 `expected` field), `851ed50` (reserve `u8 u16 u32 u64 i8 i16 i32 i64 bool unit` as keywords), `34de06a` (conversions `u64(x)`), `e95b26c` (array literals, `[v; N]`, the `'['` mode peek), `d5b8800` (named args `Point(x: 1)`), `97c2ef9` (one `relational` level, E0108, two-edit fix-it), `7ebc8dc` (E0109 for `-> unit`, the `(return_type)` dump), and two harness fixes `2b8b150`/`db4f712` (expression and block mode report leftover tokens).
>
> **Out of scope.** The lexer beyond the ten new keywords, the arena, anything under `kernel/`, and every later-stage check (struct-construction field rules, array and conversion typing, `bool`<->integer rejection, lvalue validity): those stages do not exist yet. A finding that something semantic is not rejected is not a parser bug.
>
> **The author's claims (try to disprove each).** 346 parser goldens, 15 lexer goldens, 12 `parser_fixes_test` cases; determinism 361/361; plain and ASan builds all green; coverage 99.69% with 7 uncovered lines, each justified; CI green on every pushed commit (last run 38083112051); libFuzzer parser 600 s / 469,740 execs and lexer 180 s / 2,545,004 execs clean, and the last-resort backstop never reached. Nesting depth is bounded by one shared counter (256 units): 255 deep succeeds and 256 fails with E0103, measured for nested conversions, arrays, repeat values and named-arg values, each alone. Every one of the 42 productions of the grammar in `M2_PLAN_s4.md` has a golden.
>
> **The pack (7 files).** `BUILD.txt`: split, build and run commands; read it first. `step4.diff`: the diff above. `code_bundle.txt`: all 20 files of `tools/mannac/` at HEAD, each under `===== FILE: tools/mannac/<name> =====`. `tests_bundle.txt`: every test file added or changed since `m2-parser-r1` (358 files) plus the `.mn` input of each `.ast`/`.tokens`, same header format (a header ending `(no trailing newline)` marks a file whose final newline the bundler added). `M2_PLAN_s4.md`: the grammar and design record (section 4 of the plan). `error_codes.md`: the diagnostic registry. `tests_parser_list.txt`: the names of all parser goldens, including unchanged ones that are not bundled.
>
> **How to run.** Follow `BUILD.txt`: split the two bundles into a work directory, build `parser_dump` (plain) and `parser_dump-asan` with the exact flags given, and run any input with `./parser_dump file.mn`. A golden is `file.mn` + `file.ast` and passes when the output equals the `.ast`. If you are on Linux, also run the ASan build with `ASAN_OPTIONS=detect_leaks=1` and report the result (the author's leak check was macOS `leaks` only; Linux LSan was never run).
>
> **Rules of evidence.** Every finding needs: (1) the complete input file, (2) the exact command, (3) the actual output from the plain build and, for crashes or memory issues, the ASan build, (4) what the correct output is and why, citing `M2_PLAN_s4.md` or `error_codes.md`. No finding without a reproduction. Separate CONFIRMED from SUSPECTED; never present a suspicion as a finding. Do not suggest redesigns; report behavior.
>
> **Where to look hardest.**
> 1. Error recovery. The parser drops a malformed statement or item and resumes at a synchronization point (`sync_skip`, `is_in_any_active_sync_set`, `resets_bracket_mask`, `handle_header_failure`, `recover_else_branch`, `sync_recover_*`). The rule: `(`/`[` depth masks only `;`; `{`, `}`, `fn`, `struct` and statement keywords reset it; the failing token never counts as an opener; a failing `{` does. Find inputs where one mistake produces a second spurious diagnostic, where a following valid declaration or statement is silently lost, or where the loop does not make progress. The new syntax (`[...]`, `(...)` conversions, named args, `->`) is the new attack surface; the previous audit found five cases here that all came from one wrong combined-depth idea.
> 2. Grammar and LL(1). `M2_PLAN_s4.md` claims one token of lookahead decides everything, with `'['` now both an index operator and an array-literal start, and the nine type keywords now both types and conversion starts. Find an input that parses ambiguously or differently from the grammar, a production that accepts something the grammar forbids (or rejects something it allows), or a statement-start/`starts_postfix` mismatch.
> 3. Depth bound. The counter must turn any deep input into the clean E0103 error, never a host stack overflow. Try mixes of constructs and the recovery paths (recovery re-enters `parse_if_stmt`/`parse_block`), nesting reached only through error paths; try to overflow the C stack or corrupt the counter.
> 4. Diagnostics and fix-its. E0101 (same-class chained comparison) versus E0108 (mixed class) and E0109 (`-> unit {`): wrong code for an input, wrong span, a fix-it whose edits do not produce the claimed text, or positions wrong for parenthesized or multi-line operands. Fix-its are not printed in the goldens; they are pinned only in `parser_fixes_test.c`, so check the arithmetic yourself.
> 5. Spans. Every node records start and end line:col. Look for wrong, inverted or unwidened spans (a parenthesized expression's span includes its parens; the E0108 fix-it depends on it).
> 6. The harness. `parser_main.c` routes by first token (file / block / expression) and reports leftover tokens in block and expression mode. Find an input that is mis-routed, a leftover that goes unreported, or a false "expected end of input".
> 7. Lexer reservation. `u8 u16 u32 u64 i8 i16 i32 i64 bool unit` became keywords, whole-token only. Look for a boundary case (`u8x`, `bool_`, digits, underscores, adjacent punctuation) that lexes wrongly, and for any place that still treats them as identifiers or breaks on them.
> 8. Memory and state. `NodeList` growth, arena use, `ParserState` reuse across parses, anything the ASan/UBSan build flags. Try large inputs (many elements, very long lines, many statements).
> 9. Tests that test nothing. Find a golden whose name says it checks X but whose input does not exercise X, or whose expected output encodes a bug. Compare `step4.diff` hunks that touch recovery against the goldens that supposedly cover them.
>
> **Known and accepted (do not report as new):** `g(1 +; 3)` and `[1 +; 3]` give two diagnostics (an error already inside an opened bracket reaches `;`); `[IDENT] fn` after a recovery skip makes the array-shaped `[y]` the next function's attribute (`M2_PLAN_s4.md`, "Recovery").
>
> **Deliverable.** After the first line: a numbered list of findings in severity order (crash / wrong tree or lost code / wrong or missing diagnostic / wrong fix-it / test gap / cosmetic), each marked CONFIRMED or SUSPECTED with the evidence above; then a short list of areas you checked and found sound, with what you ran; then anything you could not check and why.

### Author-side note (not in the prompt): current behaviour of a stray `fn` in a function body

It was listed as "known and accepted", but no user decision accepting it exists. `docs/history/PARSER_STEP4.md` only records it as "Observed, pre-existing (also at `m2-parser`), not a regression and not fixed", and the Phase A triage item #9 (`stray-fn golden`) concerned a different input (`err_recovery_one_stray_token_one_diagnostic`, a stray `@`, which produces one diagnostic). No golden has a nested `fn`. So it is now outside the prompt: the auditor is not told it is accepted. What the build prints today (plain; `parser_dump-asan` run on all three inputs: byte-identical output, exit 0, no sanitizer report), for `fn f() { let x = 1; fn g() {} let y = 2; }` (one `.mn` line per source line, `manna v0;` first):

```
ERRORS:
4:5 expected expression, found 'fn'
5:5 expected struct or function declaration, found 'let'

TREE:
(function_decl@4:5-4:14 g
  (params) (return_type) (block@4:12-4:14))
```

`f` is absent from the tree; `g` is handed to the top level; `let y` at top level gives the second diagnostic. The same two diagnostics and the same lost `f` occur with a non-empty `g` body and with a following `fn h() {}` (which is preserved). If the auditor reports this, it is a real, pre-existing finding, not a duplicate. Tracked as post-audit finding #0 in `docs/STATUS.md` "Active work".

## 5. After the audit

Findings are fixed as ordinary post-audit commits (golden that fails before and passes after, with the removed/added lines of any changed golden listed), the verification snapshot in `docs/STATUS.md` is re-run, and only then is `m2-parser-v0` tagged. The tag condition (every §4 production has a golden) is already met; see `docs/history/PARSER_STEP4.md`.
