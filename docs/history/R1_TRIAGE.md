# Parser r1: fresh-eyes audit triage

Working document for the post-`m2-parser` audit triage - not a permanent owner file
(`docs/README.md`'s table doesn't list it). Lives until r1 is tagged (`m2-parser-r1`),
then moves to `docs/history/` same as any other superseded working doc. Tracks status
against the triage plan the user gave verbatim below; **do not re-derive scope from
memory - re-read this file's verbatim section against current repo state first.**

## Status

- **Phase A (bugs): DONE.** Commit `da9f01c` ("Parser r1 Phase A: fresh-eyes audit bug
  fixes"), pushed, CI green. All 7 items (#1, #2, #7, #9, #16, #17, #18) resolved or
  confirmed-not-a-bug; see that commit's message for the full per-item account. Exit
  gate passed: fuzz both targets (lexer 1,721,110 execs/121s, parser 239,564 execs/
  300s, zero crashes, last-resort trap never hit), `make test-determinism` clean,
  coverage unchanged at the two pre-existing justified gaps (parser.c 99.81%, lexer.c
  99.64%).
- **Phase B (spec only): DONE** (`2ee8fa8`, pushed). Spec edits
  in `docs/M2_PLAN.md` (§2, lexer decisions, §4 grammar + new "Reserved words,
  constructors, and conversions" subsection, AST/dump format, §8 Admission Test
  records) and `docs/error_codes.md` (E0108, E0109, documented E0105 exceptions). No
  parser or lexer code, no golden touched. **Decision (user, start of Phase B): the
  #5/#6 code - the new `&&`/`||` comparison error (E0108), the `-> unit` error
  (E0109), and the `(return_type)` dump change - is deferred to the post-r1 #4 parser
  step, with the rest of #4.** Phase B only specs them. **Phase B decisions - ALL CONFIRMED
  (user, Phase B review):** (1) #5 reverses the formerly-accepted `a<b==c` /
  `a==b<c` (the checklist below flips those goldens); (2) the `[v; N]` count is a
  `NUMBER` token, not an expression; (3) conversions trap on any out-of-range value,
  including same-width sign changes, not only narrowing; (4) dump shapes
  `(array …)`, `(array_repeat …)`, `(convert T x)`, `(named_arg …)`; (5) the in-bracket
  recovery cascade (`g(1 +; 3)`, `[1 +; 3]`) is **accepted as a known limitation**,
  no counter added; (6) formatter rule (b), `[v; 1]` -> `[v]`; (7) `[v; 0]` is legal,
  a zero-length array - the only spelling of one, so no list-form overlap, and `v` is
  evaluated exactly once for any N (M2_PLAN §4 grammar comment).
- **Phase C (docs): DONE** (`cf54db5`..`e5137cd` plus this archive commit; CI green, run 37868443968).
- **#4 parser implementation + the #5/#6 code, and every deferred item:** MOVED to
  `docs/history/PARSER_STEP4.md` (the live working doc for that step). Nothing about #4 work is tracked here any more.

## Phase C #11: fix-hint candidates that need code (PENDING DECISION - not Phase C docs)

Phase C documented every no-hint code as an explicit exception (`docs/error_codes.md`).
Anything below needs a `parser.c`/`diag.c` change plus tests, so per the #11 rule it is
listed here with its cost and **not implemented**. Fix-its are not printed in the
`.ast` goldens (only message text is), so each would be pinned by new cases in
`tools/mannac/parser_fixes_test.c`, not by regenerating goldens.

| Candidate | Where | Cost | Caveat |
|---|---|---|---|
| Insert `;` for "expected ';' ..., found X" | 8 `diag_expected(..., "';'", ...)` sites in `parser.c` | new `diag_expected_with_fix` + the previous token's end span at each site, ~8 call edits, ~8 `parser_fixes_test.c` cases | The insertion point (end of the *previous* token) differs from the error span (the found token); wrong when the real mistake is elsewhere (`let x = 1 2;`) |
| Insert `)` for "expected ')' ..." | 3 sites | same shape, ~3 cases | Position ambiguous when the missing `)` isn't at the found token |
| Insert `]` / `}` | 5 sites | same | `}` is the least safe: where it belongs is a structural guess |
| E0106: replace the version with `v0` | `diag_unsupported_version` (one site) | ~3 lines + 1 test | Asserts v0-compatibility the tool can't know; registered as a documented exception instead |
| **E0102 `expected` field is wrong at the `for` site** (says `'{'`, parser wants `';'`) | `diag_assignment_as_condition` hardcodes it; 3 call sites | add an `expected` parameter, 3 call edits, 1-2 `parser_fixes_test.c` cases | Not a missing hint - an inaccuracy in existing JSON output; noted in the E0102 registry row |

**Decision (user, Phase C review): implement no insert-style fix hints** (`;`, `)`, `]`, `}`) and no E0106 rewrite. The E0102 `expected` bug goes on the #4 checklist and into M2_PLAN "Known limitations" (done).

## Verbatim triage plan (as given)

> Fresh-eyes audit triage. 3 phases, each its own commit(s), STOP tier.
>
> PHASE A — bugs (golden per fix):
> #1 header recovery: return a distinct "recovered past body" status;
>    block loop continues without skipping. Cover if/while/for + else.
> #2 pop the for-header ';' mask BEFORE parsing the recovered body.
> #7 meaning: only a literal directly negated is i64::MIN; -(2^63) is an error (IR stage;
>    record it). Mechanism: a parenthesized flag + span widened to include the parens
>    (no Paren node).
> #9 stray-fn golden: a cascade = >1 diagnostic from one stray token. If so, fix it.
> #16 multi-line token end_col; E0102 for `for ...; i = 5;`; a lexer-error first token
>     must not report E0107.
> #17 reset sync counters at parse entry; document the lexer-copy assumption.
> #18 sync_skip also tracks ( and [ depth: ';' inside brackets/parens is never a
>     statement boundary (required for the future [v; N]).
> Exit gate: fuzz both targets (last-resort trap on), test-determinism, coverage.
>
> PHASE B — spec only (M2_PLAN.md, admission test recorded; no parser code for #4):
> #4 struct construction Point(x: 1, y: 2): all fields, declaration order; grammar
>    arg ::= expression [ ':' expression ] (IR checks the left side is a bare name).
>    Arrays [1, 2, 3] + repeat [v; N]; '[' added to primary's FIRST set. Conversions
>    u64(x), narrowing traps. Reserve primitive type names (u8..i64, bool).
> #5 at most one comparison/equality per operand of && / ||; dedicated error, fix-it
>    adds parens.
> #6 reserve `unit`; `-> unit` is an error (omit instead); dump an absent return type as
>    (return_type).
> #3 lvalue validity = IR check #9.
> Every new diagnostic: registry code + fix hint, or a documented exception.
>
> PHASE C — docs:
> #8 canonical interchange -> "planned" (needs an S-expr reader); comment trivia for
>    the formatter -> docs/IDEAS.md.
> #10 VISION scorecard: unbuilt claims -> "designed, not yet implemented".
> #11 cheap fix hints; document exceptions.
> #12-15 stale comments/docs, STATUS order, real determinism count, coverage listing,
>    E0103 explanation.
>
> Don't move m2-parser. Tag m2-parser-r1 when done.
> #4 parser implementation follows as its own step after r1.

## Locked Phase B decisions (ready to write into M2_PLAN.md when B starts)

These are decisions, not open questions - Phase B's job is recording them in
M2_PLAN.md (grammar, admission test, registry entries), not re-deciding them.

- **#4 Struct construction:** `Point(x: 1, y: 2)` syntax. All fields required,
  declaration order (not named-arg reordering). Grammar: `arg ::= expression
  [ ':' expression ]` - the `:` form's left side must be a bare name (`x`), checked by
  the IR stage, not the parser (the parser has no field-name table yet). Needs `'['`
  added to `primary`'s FIRST set for the sibling additions below.
  - **Arrays:** `[1, 2, 3]` literal.
  - **Repeat literal:** `[v; N]` - the construct `sync_skip`'s r1 bracket-depth fix
    (#18, already landed in Phase A) was specifically made forward-compatible with:
    the literal `;` inside `[...]` must never be mistaken for a statement boundary
    during error recovery.
  - **Conversions:** `u64(x)`-style explicit conversion syntax. Narrowing conversions
    trap at runtime (consistent with the existing "overflow traps, no silent
    wraparound" rule - VISION.md).
  - **Reserved primitive type names:** `u8`..`u64`, `i8`..`i64`, `bool` (and `unit`,
    per #6 below) become reserved words, not ordinary identifiers - affects the
    lexer/keyword table, not just the parser grammar.
- **#5 Non-associative comparison inside `&&`/`||`:** at most one comparison/equality
  operator is allowed per operand of `&&`/`||` (mirrors the existing top-level
  non-associative comparison rule, E0101, extended to apply per-operand here too).
  Dedicated error, with a fix-it that adds parens to disambiguate.
- **#6 Reserve `unit`:** `unit` becomes a reserved type name. `-> unit` as an explicit
  return-type annotation is itself a compile error (the absence of `-> type` already
  means unit, stated not inferred, per the existing convention) - write `-> unit`
  the same as not writing a return type is wrong; omit the annotation instead. The
  dump format for an absent return type changes to `(return_type)` (no `unit` token
  inside the parens - this is a **dump-format change**, so every existing golden
  with a unit-returning function needs re-verifying against this new format, not
  just the newly-added ones).
- **#3 lvalue validity:** deferred to the IR stage as its own check (numbered #9 in
  whatever the IR stage's own check-numbering turns out to be - this triage doesn't
  fix that numbering, just records that lvalue validity is IR-stage work, not
  parser-stage).
- **Every new diagnostic Phase B's own code introduces** (there shouldn't be parser
  code changes in Phase B per the plan above, but if an admission-test example or a
  grammar change implies one): registry code (`docs/error_codes.md`) + a fix hint, or
  an explicit documented exception for why it has none - no diagnostic ships
  unregistered.
