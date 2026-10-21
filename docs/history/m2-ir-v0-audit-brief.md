# m2-ir-v0 audit brief (the typed IR stage, and the tagged-parser changes made for it)

Written at the close of typed-IR step 16, for an external, independent audit before the tag `m2-ir-v0` (name proposed; **the tag does not exist**). Same shape as the v0 kits (`v0-audit-brief.md`, `v0fix-audit-brief.md`): a pack, an auditor prompt, a reproduce step. Archived working record, not a source of truth: if this and `docs/M2_PLAN.md` / `docs/STATUS.md` disagree, those win.

## 1. What is being audited

Everything that changed between the tag `m2-parser-v0.1` (`ee46cbc`) and `main`: 78 commits up to the audited head `387d138` (`git rev-list --count m2-parser-v0.1..387d138`). Of the commits after the step 16 head `6b3f537`, `62012c5` is docs-only (it records that head's CI run) and `387d138` is Makefile-only (`make coverage` deletes stale profiles first; a duplicate `.PHONY` entry removed; no source, test or golden changed, and every number below is unchanged by it). The audited code is `387d138`; a later docs-only commit may record this brief's own update, `git diff --shortstat m2-parser-v0.1 HEAD -- tools/mannac`: 20 files, +6787 / -220, plus 870 new test files (`tests/ir`: 828, `tests/ir_fix`: 42) and the Makefile, CI and tools scripts. Three kinds of change, to be audited differently:

### 1.1 New: the typed IR stage (the main audit target)

`ir.h ir_internal.h ir.c ir_dump.c ir_build.c ir_decls.c ir_expr.c ir_body.c ir_calls.c ir_names.c ir_rename.c` (3,484 lines by `wc -l` over those eleven files; 5,816 over every `ir*.[ch]` including the harnesses below), plus the harnesses `ir_main.c` (the golden dumper), `ir_test.c` (91 PASS lines across its three runs), `ir_ndebug_test.c`, `fuzz_ir.c`, and the proof scripts `tools/audit_ir_poison.py`, `tools/check_error_code_coverage.py`, `tools/measure_ir_stack.sh`, `tools/measure_ir_build_stack.sh`, `tools/run_ir_tests.sh`, `tools/run_ir_fix_tests.sh`, and the golden generators `tools/gen_ir_goldens_step*.py`, `tools/gen_ir_fix_cases_step*.py`. The stage takes the parser's tree (only when the parse reported zero diagnostics) and either builds a typed IR module or reports `E02xx` diagnostics. The semantics are `docs/M2_PLAN.md` "Typed IR (stage 3): decided rules"; the plan and step record are `docs/history/TYPED_IR.md`.

### 1.2 Changed: the tagged parser components (audit these as changes to audited code)

The only tagged files that differ from `m2-parser-v0.1` are the following. `lexer.c/.h`, `ast.c/.h`, `arena.c/.h`, `parser_state.c/.h`, `parser.h`, `harness_mode.h`, `tests/lexer` and `tests/parser` are **byte-identical** (`git diff m2-parser-v0.1 HEAD` on them is empty; checked at the writing of this brief).

| Change | Commit | What it is | The claim to try to disprove |
|---|---|---|---|
| **10b: parser stack trim** | `bda0bb2` (`parser.c`, +243 / -208) | Frame-size only (its commit message: no change in behavior, grammar, diagnostics, spans or the bounds; `depth_enter`/`depth_exit` stay where they were): `E0105` is built in a cold non-inlined `err_expected()`; binary, unary and named-argument node construction moved to non-inlined helpers; `parse_primary` became a thin dispatcher over one function per kind with the work after each recursive call in `finish_call`, `finish_paren`, `finish_array`, `finish_conversion`; `parse_postfix` split its loop into `parse_postfix_tail`; the second-operator error path of `parse_relational` is a cold helper. Found when the IR builder's stack proof ran the parser on 254 nested calls under ASan: 4.9 MB of 8 MB (1.7x, under the 4x gate). | Behavior-neutral: old and new parser output identical on 10,001 inputs; macOS ASan nested calls 4.9 MB -> 886 KB (9.2x margin); Linux gcc ASan (a reviewer's run) 1,014 KB -> 367 KB. Look for any input where the trimmed parser differs from the tagged one (the old file is `git show m2-parser-v0.1:tools/mannac/parser.c`), and for a new recursion path that bypasses a depth hook. CI run 39171358203. |
| **14b: `DiagFix` is a flexible edit list** | `775533f` (`diag.h`, `diag.c`) | `MAX_FIX_EDITS` (2) and the fixed `edits[2]` array are gone: `DiagFix` ends in a flexible array allocated by `diag_fix_new(arena, capacity)`. The parser's E0108 (two edits) and E0109 (one) fix-its are built through it; the IR's rename fix-its need many edits. | Behavior-neutral for the parser: the sha256 of the full `parser_dump` and lexer output of every golden input (376 + 15) is unchanged before and after; `parser_fixes_test` (20 cases) and `parser_state_test` pass unchanged plain and ASan. Look for an allocation-size or off-by-one error in `diag_fix_new` and its callers, and for a parser diagnostic whose fix-it differs. |
| **diag.h / diag.c: one constructor per IR code** | every IR step | Additive: `diag_ir_*` constructors appended at each step (together with 14b, `diag.c` +461 and `diag.h` +195 lines since the tag), none touching an existing parser constructor. | Look for a message that does not match `docs/error_codes.md`, a missing span, or a constructor that allocates wrongly. `make test-ir-error-codes` checks that every documented emitted code has a constructor. |
| `height_probe.c`, `parser_fixes_test.c` | `b4bc92b` | Test-only: free a probe's input, a message fix, `static` storage for a `ParserState` that survives a `setjmp`. | None expected. |
| Comment-only link repoint | step 16e | `docs/TYPED_IR.md` moved to `docs/history/TYPED_IR.md`; the comments naming it were updated in `parser.c`, `diag.h` and the IR files. | `clang -E -P` output of `parser.c`, `diag.c`, `ir*.c`, `fuzz_ir.c` and `ir_test.c` is identical before and after (checked). |

### 1.3 Changed: build, CI and tools

`Makefile` (IR targets, `coverage` now also runs `tests/ir_fix`, `fuzz-ir`, `fuzz-ir-ungated`, `test-ir-ndebug`), `.github/workflows/ci.yml` (the new steps), `tools/fuzz/run_fuzzers.sh`. Out of scope: anything under `kernel/`, the lexer, the arena.

## 2. State at the time of writing (all from running the commands, 2026-10-20)

- IR goldens 414 (`tests/ir/*.mn` + `.ir`), plain and ASan; 17 rename cases (`tests/ir_fix`); 91 IR unit-test PASS lines plain and ASan (66 in the 8 MB run, 10 + 15 in the two 2 MB stack runs); 2 NDEBUG tests; determinism 822/822; every one of the 22 CI test targets exits 0 locally. CI: run 39499256968 on `6b3f537` (the step 16 head) and run 39628702872 on `387d138` (the audited head, attempt 1) are both green, all steps including `audit-ir-poison` (104/104 on `macos-14`) and `test-ir-ndebug`.
- `make audit-ir-poison`: 104 poisoning lines, each run by a golden whose expected output holds exactly one diagnostic. `make test-ir-error-codes`: 44 documented-emitted codes, 43 with a golden, 25 with a unit test, all 44 with a `diag_ir_*` constructor in `diag.c`; `E0201` (the IR arena limit) has no golden (only a generated input reaches it; unit tests use a 64 KiB arena); `E0202` is unassigned on purpose.
- Stack (gate: passes at 2048 KB of 8192 KB): builder worst margin 94.2x plain, 9.4x ASan; dumper 103.7x plain, 14.7x ASan; parser alone on the same inputs 31.6x plain, 7.2x ASan.
- Fuzz (libFuzzer + ASan + UBSan): `fuzz_ir` gated 601 s / 228,502 execs, ungated 601 s / 234,347 execs, 0 crashes, 0 timeouts. The ungated mode removes the parse-diagnostics gate; over the 1,747-file corpus it ran the IR on 1,448 inputs that the parser had reported errors on (644 BUILT, 804 ERRORS, 0 `IR_UNIMPLEMENTED`; 668 BUILT and 1,012 ERRORS over all 1,680 inputs that parsed without a fatal abort), with no sanitizer report. The input classification and BUILT/ERRORS counts come from a throwaway program (parse, then `ir_build` built with `-DMANNA_FUZZ_IR_UNGATED` and ASan/UBSan, over `tools/fuzz/corpus/ir/*`) that is not in the repository or the pack; the execs come from `build/fuzz/fuzz_ir_{gated,ungated}.log`, which are not tracked either.
- Coverage (`make coverage`, 99.18% of 5,137 lines): every uncovered line in `ir*.c`, `lexer.c`, `parser.c` is justified in `docs/STATUS.md` "Verification snapshot: typed IR".
- Author-side limits: only macOS Apple clang 15 was available; the IR Make targets honor `MANNAC_CC`, so `make MANNAC_CC=gcc test-ir-unit-asan` is the Linux run. Reviewer Linux runs are recorded in `STATUS.md` for step 9 and for 10b; none exists for steps 11-16.

## 3. The pack and the reproduce step

`~/Desktop/m2-ir-v0-audit/`:

| File | Contents |
|---|---|
| `BUILD.txt` | split, build and run commands (read first) |
| `ir.diff` | `git diff m2-parser-v0.1 HEAD -- tools Makefile .github ':!tools/fuzz/corpus'` |
| `code_bundle.txt` | every file of `tools/mannac/` at HEAD, the `tools/*.sh` and `tools/*.py` scripts, `tools/fuzz/run_fuzzers.sh`, `Makefile`, `.github/workflows/ci.yml`, each under `===== FILE: <path> =====` |
| `tests_bundle.txt` | every file of `tests/ir` and `tests/ir_fix` (all new since the tag), same header format |
| `tests_ir_list.txt` | `ls tests/ir` |
| `TYPED_IR.md`, `error_codes.md`, `M2_PLAN_ir.md`, `M2_PLAN_s4.md`, `STATUS.md` | the plan record, the code registry, the IR rules and the Admission records (#7-#15), the parser grammar (to judge 10b), and the status with the coverage justification |
| `m2-ir-v0-audit-brief.md` | this file |

A golden is `file.mn` + `file.ir` (`tests/ir`) and passes when `ir_dump file.mn` equals the `.ir`; a rename case is `NAME.mn`, `NAME.fixed.mn` (and optionally `NAME.remaining`), run through `ir_dump --fixed`. A finding must be reproducible with the binaries `BUILD.txt` builds.

### Regenerating the pack (maintainer step; from a clean checkout of the commit under audit)

```bash
D=~/Desktop/m2-ir-v0-audit; mkdir -p $D
git diff m2-parser-v0.1 HEAD -- tools Makefile .github ':!tools/fuzz/corpus' > $D/ir.diff
python3 - <<'PY'
import os,glob
D=os.path.expanduser('~/Desktop/m2-ir-v0-audit')
def bundle(paths,out):
    with open(out,'wb') as f:
        for p in paths:
            b=open(p,'rb').read(); nl=b.endswith(b'\n') or not b
            f.write(("===== FILE: %s%s =====\n"%(p,"" if nl else " (no trailing newline)")).encode()+b+(b"" if nl else b"\n"))
code=sorted(glob.glob('tools/mannac/*'))+sorted(glob.glob('tools/*.sh')+glob.glob('tools/*.py'))+['tools/fuzz/run_fuzzers.sh','Makefile','.github/workflows/ci.yml']
bundle(code, D+'/code_bundle.txt')
bundle(sorted(p for p in glob.glob('tests/ir/*')+glob.glob('tests/ir_fix/*') if os.path.isfile(p)), D+'/tests_bundle.txt')
PY
ls tests/ir > $D/tests_ir_list.txt
cp docs/history/TYPED_IR.md docs/error_codes.md docs/STATUS.md docs/history/m2-ir-v0-audit-brief.md $D/
awk '/^### Typed IR \(stage 3\)/{p=1} /^## 5\. Test strategy/{p=0} p' docs/M2_PLAN.md > $D/M2_PLAN_ir.md
awk '/^## 8\. Feature Admission Test records/{p=1} /^## Working log/{p=0} p' docs/M2_PLAN.md >> $D/M2_PLAN_ir.md
awk '/^## 4\. Parser grammar/{p=1} /^### Typed IR \(stage 3\)/{p=0} p' docs/M2_PLAN.md > $D/M2_PLAN_s4.md
# BUILD.txt: kept with the pack (split + build + run)
```

## 4. Auditor prompt

This block is the whole prompt and the only thing pasted to the reviewer. `~/Desktop/m2-ir-v0-audit-prompt.txt` is this block with the leading `> ` removed (generated, not hand-edited).

> FIRST LINE OF YOUR ANSWER: could you build and run the pack? yes/no, with the build output if no. If no, still audit by reading the code and tests: mark every finding SUSPECTED and give your predicted output for it.
>
> You are auditing the type checker ("typed IR stage") of a small systems-language compiler (Manna v0), written in C, plus two changes made to its already-audited parser. The author's claims in this prompt were produced by the people who wrote the code; assume any of them can be wrong. Do not trust a golden test because it exists or passes: this project has already shipped a regression whose golden was re-baselined to match it, and the IR's goldens were written by scripts the author also wrote. Your job is to find what is wrong, not to confirm what is right.
>
> **What is being audited.** (1) The new IR stage: `tools/mannac/ir*.c` and `ir*.h`, the diagnostics it emits (`E02xx`, `docs/error_codes.md`), its rename fix-its, and its tests. It takes the parser's tree, only when the parse had zero diagnostics, and builds a typed IR or reports errors. The rules are in `M2_PLAN_ir.md` ("Typed IR (stage 3): decided rules" and the Admission records #7-#15); the plan record is `TYPED_IR.md`. (2) Two changes to code that was audited at the tag `m2-parser-v0.1`: the parser stack trim (`bda0bb2`, `parser.c`: claimed behavior-neutral) and `DiagFix` becoming a flexible edit list (`775533f`, `diag.h`/`diag.c`: claimed behavior-neutral for the parser), plus constructors added to `diag.c` for every IR code. The old parser is `git show m2-parser-v0.1:tools/mannac/parser.c` (it is not in the pack; reconstruct it from `ir.diff`, which reverses cleanly with `patch -R`).
>
> **Out of scope.** The lexer, the arena, `kernel/`, and every later stage (loop-counter injection, the verifier, the Wasm backend). A check that belongs to a later stage and does not exist yet is not an IR bug (bounds proofs, runtime traps, constant folding).
>
> **The author's claims (try to disprove each).** 414 IR goldens, 17 rename cases, 91 unit-test PASS lines (66 + 10 + 15, see `BUILD.txt` step 4), plain and ASan, all pass; determinism 822/822; the no-cascade audit (`make audit-ir-poison`) finds 104 poisoning lines, each run by a golden with exactly one diagnostic; of the 44 documented emitted codes (`python3 tools/check_error_code_coverage.py`), 43 have a golden, 25 have a unit test, and all 44 have a `diag_ir_*` constructor in `diag.c`; `E0201` (the IR's own 16 MiB arena limit) has no golden, because only a generated input of that size reaches it (`docs/error_codes.md`), and it is covered by unit tests that use a 64 KiB arena (`test_e0201_and_reuse`); stack margins at least 4x everywhere (builder worst 9.4x under ASan); libFuzzer `fuzz_ir`, gated 228,502 execs and ungated 234,347 execs (the ungated mode removes the parse-diagnostics gate), no crash; 99.18% line coverage with every uncovered line justified (`STATUS.md`); the lexer, AST, arena and `parser_state` sources, `parser.h`, `harness_mode.h` and every golden under `tests/lexer` and `tests/parser` are byte-identical to the tag (an empty `git diff m2-parser-v0.1 HEAD` on them). Everything else that differs from the tag, in the parser and its tests, is: `parser.c` (`bda0bb2`, plus two comment-only documentation-path edits in step 16e, `clang -E -P` output identical), `diag.h` and `diag.c` (`775533f` and the IR constructors; `diag.h` also has the same comment-only edit from 16e), and `height_probe.c` and `parser_fixes_test.c` (`b4bc92b`, test-only).
>
> **The pack.** `BUILD.txt`: split, build and run commands; read it first. `ir.diff`: everything changed since the tag. `code_bundle.txt` and `tests_bundle.txt`: sources and goldens under `===== FILE: <path> =====`. `tests_ir_list.txt`, `TYPED_IR.md`, `error_codes.md`, `M2_PLAN_ir.md`, `M2_PLAN_s4.md`, `STATUS.md`, and this brief.
>
> **How to run.** Follow `BUILD.txt`: build `ir_dump` (plain and ASan) and `ir_test`, and run any input with `./ir_dump file.mn` (add `--fixed` for the rename fix-its). If you are on Linux, also run the ASan builds with `ASAN_OPTIONS=detect_leaks=1` and report the result.
>
> **Rules of evidence.** Every finding needs: (1) the complete input file, (2) the exact command, (3) the actual output from the plain build and, for crashes or memory issues, the ASan build, (4) what the correct output is and why, citing `M2_PLAN_ir.md` or `error_codes.md`. No finding without a reproduction. Separate CONFIRMED from SUSPECTED; never present a suspicion as a finding. Do not suggest redesigns; report behavior.
>
> **Where to look hardest.**
> 1. One mistake, one error. The stage poisons a failed expression with an ERROR type so that nothing built on it reports again. Find a program with exactly one mistake that produces two or more diagnostics (a cascade), or a mistake that produces none. Also the opposite: two independent mistakes in one function that produce one error.
> 2. Types and literals. Integer literals have no default type (`let x = 5;` is an error); a literal takes its type from context or from a typed sibling; `-128` is a valid `i8` but `-(128)` is not; groups of literals; the `i64::MIN` fold at every signed width; range checks per width; `256 + x` with `x: u8`.
> 3. Operators and conversions. The per-operator rules (shifts, `bool` with `&|^`, `==` on structs/arrays, unary minus on unsigned), `u64(x)` conversions (integer to integer only; `bool` either side rejected), the wrapped-operand fix-it for mixed operand types (E0261): find a fix-it that does not produce a program the checker accepts.
> 4. Names and scopes. No shadowing anywhere in a function; order-independent top level; a name means one thing. Find a way to reuse or shadow a name, a use-before-declaration the checker misses or wrongly reports, or a scope that leaks (loop-init `let`, `else` branches, sibling blocks).
> 5. Calls, recursion, `len`. Arity and argument types, named arguments, the unit rule (E0226: a call that returns nothing is not a value), direct and mutual recursion (an iterative Tarjan-style component search): find a cycle it misses, a non-cycle it reports, or a report at the wrong function. `len(x)` as a builtin (E0254-E0257).
> 6. Structs, arrays, slices and sizes. Construction rules (E0240-E0245), field access, the size limit (`2^31 - 1` bytes) and the layout computation including overflow, containment cycles through arrays, slices allowed only as parameter and local types and never nested or returned, `[v; N]` with `N == 0`, `a[lo..hi]` typing.
> 7. Missing-return analysis (E0276/E0277), `break`/`continue` outside a loop (E0275), loop scoping.
> 8. The error cap and limits. E0203 stops the build after 20 errors (recording a 21st at its span); E0201 is the IR's own arena limit. Find a program where the cap leaves a misleading state, a leak, or a fix-it offered from an incomplete use list.
> 9. Naming and the rename fix-its (E0280/E0281). A fix renames a declaration and every resolved use. Find a case where applying every offered fix produces a program that still has a naming error, fails to compile, changes meaning (a collision with an existing name), or edits the wrong text (a use before its declaration, a field named like a local, a name inside a nested block, overlapping or duplicate edits). `ir_dump --fixed` applies them; the claim is that the fixed source is accepted with zero E02xx.
> 10. Robustness. The stage must not crash on any tree the parser can produce, gated or not. Try inputs that the parser accepts with diagnostics and that the gate normally hides; try deep and wide inputs, very large literals, many locals, many functions, many errors; try to overflow the C stack (the builder is recursive on expression height; the parser bounds height at 512).
> 11. The two tagged-parser changes. Diff the parser behavior old versus new on any input you can construct (including malformed ones, deep nesting, every recovery path); look for a changed diagnostic, a changed tree, a changed span, or a depth hook that is no longer on every cycle of the call graph. For `DiagFix`: any parser diagnostic whose fix-it text or edit spans differ from the tagged build.
> 12. Tests that test nothing. Find a golden whose name says it checks X but whose input does not exercise X, or whose expected output encodes a bug (the goldens are generated by scripts that compute columns from the source; check a few by hand). Compare `tools/audit_ir_poison.py`'s claim (each poisoning line run by a one-diagnostic golden) against what the golden really does.
>
> **Known and accepted (do not report as new):** `E0202` is deliberately unassigned; `IR_UNIMPLEMENTED` is reachable only through an `ExprKind` no case handles (a state the parser never produces; the two other raisers are asserts, and with asserts compiled out they fall through to the same status, `make test-ir-ndebug`); the linear field lookup in a field access (a 20,000-field struct accessed 20,000 times takes 0.8 s to 1.7 s with a plain -O2 build, depending on which field, and 5 s to 10 s under ASan; recorded in `STATUS.md` "Known issues"); the parser-side uncovered lines listed in `STATUS.md`.
>
> **Deliverable.** After the first line: a numbered list of findings in severity order (crash / wrong IR or accepted-but-invalid program / rejected-but-valid program / cascade or missing diagnostic / wrong or unsafe fix-it / parser behavior change / test gap / cosmetic), each marked CONFIRMED or SUSPECTED with the evidence above; then a short list of areas you checked and found sound, with what you ran; then anything you could not check and why.

## 5. After the audit

Findings are fixed as ordinary post-audit commits (a golden or test that fails before and passes after, with the removed and added lines of any changed golden listed), the verification snapshot in `docs/STATUS.md` is re-run, and only then is `m2-ir-v0` tagged. Nothing here creates the tag.
