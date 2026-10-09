# Parser step #4: constructors, conversions, reserved words (+ the #5/#6 code)

Working document for the post-`m2-parser-r1` parser step - not a permanent owner file
(`docs/README.md`'s table doesn't list it). Lives until this step is tagged, then moves
to `docs/history/`. The `docs/STATUS.md` "Active work" line points here.

**Status: NOT STARTED.** Spec is done and approved (r1 Phase B, tag `m2-parser-r1`); this
step is the implementation. Do not start it without an explicit go.

## Prerequisite fixes: r1 regressions (do these before step 0)

An external audit of `da9f01c` found recovery regressions that `m2-parser-r1` shipped (the tag is not moved). Each was reproduced with a real build, plain + ASan, against `main` before anything was changed, and compared with `m2-parser` (fine there). Fixed, one commit each, **committed locally and awaiting push approval**:

| Fix | Commit | What was wrong | Goldens / tests (fail before, pass after) |
|---|---|---|---|
| R6 | `cad102b` | only `sync_*` reset at parse entry; `depth`/`error_count` stale after a fatal longjmp, so a valid second file got a spurious E0103 | `parser_fixes_test`: reset after fatal longjmp; error_count restarts |
| R1 | `3553b67` | header recovery left a trailing `else`/`else if`: 2nd diagnostic, stray statement | `err_recovery_{if_header_else,elseif_header_else,if_header_elseif_chain}_preserved` |
| R2-R5 | `e3d6652` | one combined bracket depth hid every resume token, so an unmatched `(`/`[` swallowed `}`/`fn`/`struct` | `err_recovery_{unclosed_bracket_in_block,toplevel_open_paren,unclosed_call_then_decl,open_parens_then_decls}`; `err_recovery_stress_no_infinite_loop.ast` restored 1 -> 7 diagnostics |
| R7, R9 | `c1daf01` | test leaked its arena; comments cited a nonexistent `token_span_test.c` | `leaks --atExit`: 4 leaks / 2,105,440 bytes -> 0 (Linux LSan not run) |
| tests | `f207107` | no tests for #9, #17, the `(` half of #18, or `;` inside brackets | 4 pin goldens + a sync-counter reset test (mutation-checked) |

Consequences for the checklist below:
- Step 3 (`[v; N]`): the `[1 +; 3]` cascade pin still has to be added (the call-form pin `err_recovery_in_call_cascade_accepted` exists now). The new `sync_skip` comment already names the r1-B forms as containing no `{ } fn struct` or statement keyword - keep that true.
- Any step that adds a token which can start a statement, or an expression that can contain a block, must re-check `resets_bracket_mask` in `parser.c`.
- Golden counts and determinism inputs in the plan are now 201 and 215 (were 187 and 201). The R1 recovery path's depth limit was measured (254 total ifs, same as the direct else-if chain): `depth_elseif_recovered_*`, `docs/M2_PLAN.md` table row "7 (via header recovery)".
- **Observed, pre-existing (also at `m2-parser`), not a regression and not fixed:** a stray `fn` inside a function body, `fn f() { let x = 1; fn g() {} let y = 2; }`, gives two diagnostics and drops `f` (the `fn` is handed to the top level, by the Step 10b design; compare `err_recovery_missing_rbrace_next_fn_preserved`).

## Approved step plan (one commit per step, each green on its own)

Every step also updates the `parser.c` header grammar comment (a copy of the grammar), flips the `r1-B ... not yet implemented` / "Specified, not yet emitted" markers for its own piece in M2_PLAN.md and `error_codes.md`, and is pushed with its CI run watched to green. STOP steps end with a report and wait for approval.

| # | Step | Tier | Goldens |
|---|---|---|---|
| 0 | E0102 `expected` field (`'{'` for `if`/`while`, `';'` for `for`): extra parameter on `diag_assignment_as_condition`, 3 call sites, `parser_fixes_test.c` | ROUTINE | none (goldens print message text only). **DONE, committed locally, awaiting review** |
| 1 | Reserve `u8`..`i64`, `bool`, `unit` **and** teach `parse_type` the nine `PRIM_TYPE` keywords (cannot be split: the moment `u32` is a keyword every typed golden breaks); fix the stale `lexer.h` comment; `token_kind_name`/`token_describe` cases | **STOP** (keyword reservation) | re-baseline `tests/lexer/basic.tokens`; new lexer golden for the 10 keywords; reserved-word-as-name errors; `unit` in type position; the 22 type-using parser goldens re-diffed byte-identical |
| 2 | Conversions `u64(x)` (depth cycle 10): `EXPR_CONVERT`, `parse_primary`, `starts_postfix` += 9 keywords, `ast_test.c` | **STOP** (grammar) | `convert_*`, error shapes, `depth_conv_*` at the measured limit + 300, plain + ASan |
| 3 | Array literal and `[v; N]` (depth cycle 9) **and** the `parser_main.c` mode-peek fix | **STOP** (grammar, recovery-adjacent) | `array_*`, `repeat_*` (incl. `repeat_zero`, `repeat_one`), the `[1 +; 3]` cascade pin, the `[IDENT] fn` misfire golden, `depth_array_*`/`depth_repeat_*` |
| 4 | Named args `Point(x: 1)` (cycle 3 extended), `EXPR_NAMED_ARG` | **STOP** (grammar) | `namedarg_*`, `depth_namedarg_*` |
| 5 | `relational` merge + E0108 (flip the two `comparison_wrapped_by_equality_*` goldens) | **STOP** (grammar + golden flip) | the M2_PLAN E0101/E0108 table, `(a<b)==c`, `&&`-embedded; fix-it text pinned in `parser_fixes_test.c` |
| 6 | E0109 `-> unit` + the `(return_type)` dump change | **STOP** (re-baseline) | re-baseline the 14 `(return_type unit)` goldens; `err_return_unit` |
| 7 | Proof and close-out: fuzz both targets with `MANNA_FUZZ_TRACE_LAST_RESORT`, determinism, coverage, flip the Admission Test records to "admitted", STATUS Done entry, archive this file, tag, re-minimize the fuzz corpus | **STOP** (tag) | corpus only |

Depth limits stay predictions until each step's goldens measure them (255/256/300 under plain and ASan); the R1 recovery path's was measured at 254 (see below).

## Gaps found in the original checklist (all folded into the plan above)

1. `lexer.h` still says built-in type names "are NOT keywords"; also `token_kind_name` and `token_describe` need the ten new kinds (step 1).
2. E0108/E0109 fix-its need `parser_fixes_test.c` cases; goldens never print fix-its (steps 5, 6).
3. E0108's fix-it needs the operands' source text, and `Span` carries line/col only, no byte offsets: a design decision (lean: capture start/end token lexeme pointers in the parser), STOP-level, raised in step 5.
4. No AST work was listed: new `EXPR_*` kinds, builders, printers, `ast_test.c` cases. Cite `ast.c`/`ast_test.c` lines by content, not number (they shift).
5. The `parser.c` INVARIANT back-edge count moves twice (step 2: eight, step 3: nine), not once. The recovery paths are not extra back-edges (see the INVARIANT comment).
6. "Re-diff byte-identical" for the type-using goldens had no owning step; it is step 1.
7. The `[IDENT] fn` misfire golden (M2_PLAN §4 "Recovery") was missing (step 3).
8. Fuzz-corpus re-minimization is allowed only at milestone boundaries (`docs/README.md`): done at the tag, step 7.
9. Housekeeping was missing: STATUS Done entry, archiving this file, the tag name (suggested `m2-parser-4`), and flipping the `r1-B` / "Specified, not yet emitted" markers (per step, final sweep in step 7).

## Where the spec lives (this file is a checklist, not a spec)

- Grammar, LL(1) re-verification, `'['` recovery analysis, new recursion cycles
  (limits are **predictions** until built), diagnostics E0108/E0109 with exact fix-it
  text, dump shapes: `docs/M2_PLAN.md` §4, "Reserved words, constructors, and
  conversions", and the `relational` / `primary` / `arg` / `array_body` productions.
- Admission Test records (#4, #5, #6), all "pending admission (goldens due in #4 step)":
  `docs/M2_PLAN.md` §8. Registry rows: `docs/error_codes.md` (E0108, E0109, documented
  E0105 exceptions).
- The r1 decisions this step implements (confirmed in the Phase B review) are archived
  in `docs/history/R1_TRIAGE.md` and restated in M2_PLAN: `[v; N]`'s `N` is a `NUMBER`
  token; `[v; 0]` is legal; conversions trap on any out-of-range value; dump shapes
  `(array …)`, `(array_repeat …)`, `(convert T x)`, `(named_arg …)`; the in-bracket
  recovery cascade is an accepted known limitation (no counter added); formatter rule (b).

## Checklist (moved verbatim from the archived R1_TRIAGE.md, which now only points here)

Implements #4 (struct construction, arrays, `[v; N]`, `u64(x)`, reserved primitive names) plus the #5/#6 code (E0108, E0109, `(return_type)`).

- lexer keyword table + `tests/lexer/basic.tokens`;
  `parse_type`/`parse_primary`/`starts_postfix`/`parse_arg_list`;
- `relational` merge (#5), **and flip `tests/parser/comparison_wrapped_by_equality_{lhs,rhs}`
  from valid to E0108 error goldens**; add the E0101/E0108 table cases from M2_PLAN §4;
- `-> unit` (E0109); `ast.c:541` + `ast_test.c:142` + the 14 `(return_type unit)`
  goldens -> `(return_type)`, every other golden re-diffed byte-identical;
- **`parser.c` comments:** update the stale `is_top_level_resume_token` comment
  ("headers and statements can't otherwise produce one" is false once `'['` starts an
  expression) and the INVARIANT comment's back-edge count (seven -> nine);
- **DONE (step 0): fix E0102's hardcoded `expected = "'{'"`** (wrong at the `for` condition site, where the parser wants `';'`): add an `expected` parameter to `diag_assignment_as_condition`, update its 3 call sites, pin with `parser_fixes_test.c` cases;
- goldens for `[v; 0]` (`repeat_zero`) and `[v; 1]`, and pinning goldens for the accepted
  in-bracket cascade (`[1 +; 3]`, `g(1 +; 3)`: two diagnostics each);
- **`parser_main.c`'s mode-dispatch peek routes a leading `'['` to whole-file mode**
  (it assumes `'['` can only start an attribute). Once `'['` starts an array literal,
  expression goldens like `[1, 2, 3]` would be mis-routed: the peek must be changed
  (e.g. `'['` + IDENT + `']'` + `fn`, the same shape `is_top_level_resume_token` uses);
- depth goldens (predicted 255/256/300); fuzz both targets with the last-resort trap on;
- **close the §8 Admission Test records (#4, #5, #6) from "pending admission (goldens
  due in #4 step)" to "admitted"** once their listed goldens exist and pass;
- formatter requirement (not parser work, recorded for when `manna fmt` is built):
  M2_PLAN §8 #4 item 5 array-literal canonicalization.

## Proof obligations (nothing here may be stated as fact until built and run)

- Every depth limit in M2_PLAN §4 "New recursion cycles" is a prediction: prove 255/256/300 under plain and ASan, or correct the spec.
- The LL(1) argument is by hand: one golden per decision row of its table.
- Both fuzz targets with `MANNA_FUZZ_TRACE_LAST_RESORT` on; the last-resort backstop must still never be reached.
- `make test-determinism` and `make coverage` re-run; list any new uncovered line with a justification, as at every tag.
