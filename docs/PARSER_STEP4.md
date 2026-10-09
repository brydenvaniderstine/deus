# Parser step #4: constructors, conversions, reserved words (+ the #5/#6 code)

Working document for the post-`m2-parser-r1` parser step - not a permanent owner file
(`docs/README.md`'s table doesn't list it). Lives until this step is tagged, then moves
to `docs/history/`. The `docs/STATUS.md` "Active work" line points here.

**Status: NOT STARTED.** Spec is done and approved (r1 Phase B, tag `m2-parser-r1`); this
step is the implementation. Do not start it without an explicit go.

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
- **fix E0102's hardcoded `expected = "'{'"`** (wrong at the `for` condition site, where the parser wants `';'`): add an `expected` parameter to `diag_assignment_as_condition`, update its 3 call sites, pin with `parser_fixes_test.c` cases;
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
