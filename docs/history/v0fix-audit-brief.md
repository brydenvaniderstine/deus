# v0 fixes audit brief (second audit: the six fixes only)

Written after the first v0 audit (`docs/history/v0-audit-brief.md`) came back and its findings were fixed. Archived working record, not a source of truth: if this and `docs/M2_PLAN.md` / `docs/STATUS.md` disagree, those win. The first audit's pack and brief are unchanged; this one covers only what changed after it: `git diff 581e4d4 HEAD`, parser code and tests.

## Pack (7 files, `~/Desktop/v0fix-audit/`) and prompt (`~/Desktop/v0fix-audit-prompt.txt`)

`fixes.diff` (`git diff 581e4d4 HEAD -- tools/mannac tools/fuzz`, **corpus excluded**: ~1,800 hash-named fuzz inputs, 612 KB of diff), `code_bundle.txt` (all 21 files of `tools/mannac/` plus `tools/fuzz/run_fuzzers.sh`; the fuzz target `fuzz_parser.c` lives in `tools/mannac/`), `tests_bundle.txt` (tests added or changed since 581e4d4 with their `.mn`: 36 files, 18 pairs; renames are included under their new names only), `M2_PLAN_s4.md`, `error_codes.md`, `tests_parser_list.txt`, `BUILD.txt` (verified in a fresh directory; the file itself says what it verified and what it cannot).

### Regenerating (maintainer step; from a clean checkout of the commit under audit)

```bash
D=~/Desktop/v0fix-audit; mkdir -p $D
git diff 581e4d4 HEAD -- tools/mannac tools/fuzz ':!tools/fuzz/corpus' > $D/fixes.diff
python3 - <<'PY'
import os,subprocess
D=os.path.expanduser('~/Desktop/v0fix-audit')
def bundle(paths,out):
    with open(out,'wb') as f:
        for p in paths:
            b=open(p,'rb').read(); nl=b.endswith(b'\n') or not b
            f.write(("===== FILE: %s%s =====\n"%(p,"" if nl else " (no trailing newline)")).encode()+b+(b"" if nl else b"\n"))
bundle(sorted('tools/mannac/'+x for x in os.listdir('tools/mannac'))+['tools/fuzz/run_fuzzers.sh'], D+'/code_bundle.txt')
ch={l.split('\t')[1] for l in subprocess.check_output(['git','diff','--no-renames','--name-status','581e4d4','HEAD','--','tests']).decode().strip().split('\n') if l}
ch |= {f[:-4]+'.mn' for f in ch if f.endswith('.ast')} | {f[:-7]+'.mn' for f in ch if f.endswith('.tokens')}
bundle(sorted(x for x in ch if os.path.exists(x)), D+'/tests_bundle.txt')
PY
ls tests/parser > $D/tests_parser_list.txt
cp docs/error_codes.md $D/
awk '/^## 4\. Parser grammar/{p=1} /^## 5\. Test strategy/{p=0} p' docs/M2_PLAN.md > $D/M2_PLAN_s4.md
# BUILD.txt: kept with the pack (split + build + run, verified byte-exact)
```

(`--no-renames` matters: with rename detection the new name of a renamed golden is dropped from the list.)

## The prompt

`~/Desktop/v0fix-audit-prompt.txt` is the block below with the leading `> ` removed (generated, not hand-edited).

> FIRST LINE OF YOUR ANSWER: could you build and run the pack? yes/no, with the build output if no. If no, still audit by reading the code and tests: mark every finding SUSPECTED and give your predicted output for it.
>
> You are auditing six small fixes to a hand-written recursive-descent parser for a small systems language (Manna v0), written in C. An earlier audit of the whole parser found problems; they were fixed, and you are checking the fixes, not the rest of the parser. The author's claims below were produced by the people who wrote the code; assume any of them can be wrong. Do not trust a golden test because it exists or passes: this project has already shipped a regression whose golden was re-baselined to match it, so for every changed golden compare what it expected before with what it expects now. Your job is to find inputs for which a fix does the wrong thing, or broke something it touched, and to prove each one with a real run.
>
> **What is being audited.** `git diff 581e4d4 HEAD` for the parser code (`fixes.diff`: 14 files, about +450 / -70, fuzz corpus excluded) and the tests that changed. Six fixes:
> - **F0** (`ea8b01e`): a stray `fn` or `struct` inside a block used to drop the whole enclosing function. `parse_block` now ends the block there when a top-level level is active (`at_top_level_start`, gated on `sync_top_level > 0`): the statements parsed so far are kept (the partial block's span ends at its last statement, or at the `{` if there is none), the token is left for the top level, and one diagnostic `expected '}' to close block, found 'fn'` is reported. When a statement's own failed recovery skip lands on `fn`/`struct`, nothing extra is reported (that statement's error already stands). Enclosing blocks do not repeat the diagnostic for the same token (`ParserState.unclosed_line/col`, reset per parse). Three existing goldens changed: `err_recovery_array_unclosed_then_fn` and `err_recovery_header_no_body_before_fn` each gained a `function_decl` node; `err_recovery_missing_rbrace_next_fn_preserved` changed one diagnostic's text and gained a `function_decl`.
> - **F1** (`79fbdec`): `recovered_past_body` is set when a failed `if`/`while`/`for` header's recovery parsed a body; it told the enclosing block's loop not to re-sync. It was still set while `recover_else_branch` parsed the else branch, so an error inside that branch consumed it. `recover_else_branch` now clears it for the branch and sets it again afterwards. Claimed result, with `e;` following inside `fn f`: `e;` is kept for `if x = 5 { a; } else { b d; }`, `... else { b ); }` (exactly one E0105), `... else { if y = 1 { b; } }` and `if a { } else if x = 1 { a; } else { b d; }`.
> - **F2** (`45b5a8e`): exhausting the arena used to unwind with no diagnostic. The arena now holds back a reserved tail from ordinary allocations (16 KiB for arenas of 1 MiB or more, otherwise a quarter), and runs a hook once before the `longjmp`; the hook records **E0110** `input too large: the compiler's memory limit was exceeded` (no fix-it) at the last token read (`ParserState.progress_line/col`, set in `advance`). An overflow inside the hook unwinds without calling it again.
> - **F3** (`f71a865`): `parser_dump` and the libFuzzer target now choose file/block/expression mode with one shared function (`harness_mode.h`); the fuzz target's own copy had sent every `[`-leading input to file mode.
> - **F4** (`98f8678`): a new golden `array_statement_after_if_block`, and `comparison_wrapped_by_equality_{lhs,rhs}` renamed to `err_mixed_cmp_eq_{lhs,rhs}` with no content change.
> - **F5** (`13ad71a`, `22b68ab`): comments and docs only in `parser.c`, `parser.h`, `fuzz_parser.c` (claim: identical after preprocessing, i.e. no code change), plus `static` storage for the `ParserState` that survives a `setjmp` in `parser_main.c`, `parser_fixes_test.c` and `parser_state_test.c`.
>
> **Out of scope.** Everything the fixes did not touch, the lexer, and every later compiler stage. A semantic check that does not exist yet is not a parser bug.
>
> **The author's claims (try to disprove each).** 359 parser goldens (346 + 7 stray-`fn`/`struct` + 5 else-branch + 1 row-6), 15 lexer goldens, `parser_fixes_test` 15, `parser_state_test` 5, `arena_test` 8, `ast_test` 6; plain and ASan builds all pass; determinism 374/374; `leaks` clean on the unit-test binaries; parser fuzz 600 s / 465,882 execs with the last-resort trap build, no finding; CI green (run 38091284432). Coverage 99.79% as reported, with 7 uncovered lines in effect, none in the new code.
>
> **The pack (7 files).** `BUILD.txt`: split, build and run commands; read it first. `fixes.diff`: the diff above. `code_bundle.txt`: all 21 files of `tools/mannac/` at HEAD plus `tools/fuzz/run_fuzzers.sh`, each under `===== FILE: <path> =====`. `tests_bundle.txt`: the 18 golden pairs added or changed since the base (with their `.mn`). `M2_PLAN_s4.md`: the grammar and design record. `error_codes.md`: the diagnostic registry (E0110 is new). `tests_parser_list.txt`: the names of all 359 parser goldens, including the 341 not bundled.
>
> **How to run.** Follow `BUILD.txt`: split the bundles, build `parser_dump` (plain) and `parser_dump-asan` with the exact flags given, and run any input with `./parser_dump file.mn`. A golden is `file.mn` + `file.ast` and passes when the output equals the `.ast`. If you are on Linux, also run the ASan builds and the unit tests with `ASAN_OPTIONS=detect_leaks=1` and report the result. The unit tests generate their large inputs in memory.
>
> **Rules of evidence.** Every finding needs: (1) the complete input file, (2) the exact command, (3) the actual output from the plain build and, for crashes or memory issues, the ASan build, (4) what the correct output is and why, citing `M2_PLAN_s4.md` or `error_codes.md`. No finding without a reproduction. Separate CONFIRMED from SUSPECTED; never present a suspicion as a finding. Do not suggest redesigns; report behavior.
>
> **Where to look hardest.**
> 1. **F0, the block ends at `fn`/`struct`.** Several nested blocks (`if`/`while`/`for`, else branches), with the stray token at different depths; `fn`/`struct` inside a header (`if x = 5 fn g() {}`), inside parentheses or brackets (`g(fn`, `[1, fn`), as an array element, after a recovered `else`; `[IDENT] fn` attribute shapes in a body; EOF in the same situations; block mode (`{ ... fn ... }`, where no top level is active). Look for a second or missing diagnostic that one mistake should not cause, a lost or duplicated statement or function, a wrong partial-block span, a hang or failure to make progress, a stale `unclosed_line/col` (a diagnostic wrongly suppressed because an earlier token had the same position, or wrongly repeated), and any golden among the three changed ones whose new content is wrong.
> 2. **F1, the lifetime of `recovered_past_body`.** Read every place the flag is set, read, cleared and restored (`handle_header_failure_impl`, `recover_else_branch`, the `parse_block` loop, `parser_state_reset_sync`). Try nested recoveries: a failed header inside an else branch inside a failed header, `for` headers (which have their own mask), `else if` chains of several links with the failure at different links, an else branch that is itself unclosed or hits EOF or a stray `fn`, errors in the branch that propagate out of it rather than being recovered inside it. Look for a statement dropped that should survive, a spurious second diagnostic, or the flag left set or cleared when it should not be.
> 3. **F2, E0110 and the reserved tail.** Inputs that exhaust the arena at different points (inside a call's argument list, an array, a deeply nested expression, a long chain of statements, while a diagnostic is being added, with the error list at its growth boundary of 4, 8, 16 diagnostics, with 19 or 20 errors already recorded so E0104 is next); E0103 and E0104 raised under memory pressure (does the intended diagnostic survive, is E0110 recorded in addition or instead, is the list consistent); the state after the `longjmp` (the `ParserState`, the arena, `in_hook`, `reserve`, reuse for a second parse); overflow inside the hook; `arena_alloc` with a huge size or alignment against the reduced limit (`max_size - reserve`: any wraparound or off-by-one, and a `reserve` larger than the arena); an arena smaller than its reserve. Check the reported location of E0110.
> 4. **F3, the shared mode dispatch.** Inputs the old and the new routing treat differently, in particular around `[`: attribute groups with and without `fn`, an unterminated `[`, an input that is only `[`, empty input, a first token that is a lexer error. Check that `parser_dump` and `fuzz_parser.c` really cannot diverge (they should call the same function and nothing else decides the mode).
> 5. **F4 and F5.** The two renamed goldens are content-identical to their old selves (`git diff -M`); `array_statement_after_if_block` exercises what its name says; the claim that F5's edits to `parser.c`, `parser.h` and `fuzz_parser.c` change no code (compare the preprocessed old and new files); the `static ParserState` change does not alter behavior when a test runs twice in one process.
> 6. **Tests that test nothing.** For every new golden and unit test, check that it fails on the code before the fix (the old behavior is in the `-` side of `fixes.diff`) and that its name matches what its input exercises.
>
> **Known and accepted (do not report as new):** for a *truly nested* `fn g() {}` inside a function body, the block is read as unclosed at the `fn`, `g` becomes a top-level function, and code after it inside the intended body gets its own top-level diagnostic (two diagnostics in total for `fn f() { let x = 1; fn g() {} let y = 2; }`); a block cut off by end of file still drops its function (only the `fn`/`struct` case keeps it); `g(1 +; 3)` and `[1 +; 3]` give two diagnostics (an error already inside an opened bracket reaches `;`); `[IDENT] fn` after a recovery skip makes the array-shaped `[y]` the next function's attribute; E0108 reports only the first offending pair of an expression and its message example (`(a < b) == c`) does not follow operand order, while the fix-it does.
>
> **Deliverable.** After the first line: a numbered list of findings in severity order (crash / wrong tree or lost code / wrong or missing diagnostic / test gap / cosmetic), each marked CONFIRMED or SUSPECTED with the evidence above and the fix (F0-F5) it concerns; then a short list of areas you checked and found sound, with what you ran; then anything you could not check and why.
