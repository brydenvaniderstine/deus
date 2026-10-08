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
- **Phase B (spec only): NOT STARTED.** No code, no M2_PLAN.md edits yet beyond what
  Phase A already touched. See "Locked Phase B decisions" below for what's already
  decided and ready to write up - the work itself is turning those into M2_PLAN.md
  prose/grammar/admission-test entries, not deciding them fresh.
- **Phase C (docs): NOT STARTED.**
- **#4 parser implementation** (struct construction / arrays / repeat-literal /
  conversions, from Phase B's spec): explicitly deferred to its own step **after**
  `m2-parser-r1` is tagged - do not implement it as part of Phase B or this triage.

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
