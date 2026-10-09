# Manna compiler error code registry

Stable codes for `manna check --json` (docs/M2_PLAN.md §7 item 4). A code, once assigned, is **never reused or renumbered** — if a diagnostic is retired, its code retires with it rather than being recycled for something unrelated.

Ranges, reserved by pipeline stage (docs/M2_PLAN.md §3):

| Range | Stage |
|---|---|
| `E00xx` | Lexer (assigned retroactively at JSON-schema implementation time — the lexer's existing error kinds in `tools/mannac/lexer.h` predate this registry) |
| `E01xx` | Parser |
| `E02xx` | Typed IR / type checking |
| `E03xx` | Static pointer verifier |
| `E04xx` | Codegen (loop-counter injection, Wasm backend, native backend) |

## E01xx — Parser

| Code | Message | Meaning | Fix hint |
|---|---|---|---|
| `E0101` | `comparisons can't be chained; use a < b && b < c` | Two comparison/equality operators in one expression (`a < b < c`) — non-associative by design (docs/M2_PLAN.md, comparison/equality grammar). | None yet — the rewrite isn't a single-span text replacement. |
| `E0102` | `assignment is a statement; did you mean '=='?` | `if`/`while` found `=` where the condition-terminating `{` was expected. | Replace the single `=` with `==`. |
| `E0103` | `maximum nesting depth (256 units) exceeded` | The shared counter (`MAX_NESTING_DEPTH` = 256 **depth units**, not 256 levels of any one construct) went past its cap. A single construct costs 1 unit/level in isolation, so 255 levels of it parses, 256 is the first to fail (the innermost operand itself costs the 256th unit). Mixed constructs cost more per level - e.g. `!(...)` costs 2 units/level, capping at 127. **The budget is shared with whatever already-counted construct encloses the one being measured - effective limits shrink with context.** Worked example: a nested array type costs 255 levels on its own, but inside a `let` (which only ever appears inside a `block`, itself 1 unit) it caps at 254 - one less, exactly what `block` already spent (`tests/parser/depth_type_*`). See `docs/M2_PLAN.md`'s "Universal nesting-depth bound" and `tools/mannac/parser.c`'s own INVARIANT comment. | None — the input needs restructuring, not a mechanical edit. |
| `E0104` | `too many errors, stopping` | Error count reached `MAX_ERRORS` (20) in one file; parsing stopped early as a safety valve against cascading noise. | None — fix the earlier errors first. |
| `E0105` | `expected X, found Y` (template) | Generic syntax error — every parse failure that isn't one of E0101/E0102 above goes through this one constructor (`diag_expected`, `tools/mannac/diag.c`), so the message format lives in one place. `X`/`Y` vary per call site (e.g. `expected ')' after expression, found '}'`). | Varies by call site; most have none yet. |
| `E0106` | `unsupported manna version 'vN' (expected 'v0')` | The file's `version_decl` (`manna IDENT;`, required as the first non-comment line - MANNA_PRINCIPLES.md Part 4 item 1) has `manna` present and a well-formed identifier that isn't `v0`. A `manna` keyword present but malformed after that (no identifier, no `;`) is E0105 instead; `manna` altogether absent is E0107 instead. | None yet — no v1 to suggest migrating to. |
| `E0107` | `missing version header; add 'manna v0;' as the first line` | The file's first token isn't `manna` at all (as opposed to `manna` being present but malformed or the wrong version - E0105/E0106). | Insert `manna v0;` as the file's first line (fix-it provided, inserting at 1:1). |
| `E0108` | `comparison and equality can't be mixed without parentheses; use (a < b) == c` | **Specified (r1 Phase B), not yet emitted - first emitted by the post-r1 #4 parser step.** An operand of `&&`/`\|\|` (or a lone expression) contains two comparison/equality operators of *different* class, e.g. `a < b == c` or `a == b < c`; both are accepted today and become errors. Same-class repeats (`a < b < c`, `a == b == c`, `a == b != c`) stay `E0101`, with no fix-it. docs/M2_PLAN.md §4 `relational` + "Reserved words, constructors, and conversions". | Exactly one `(` and one `)` inserted around the comparison-class pair (`<,>,<=,>=`, from its left operand's first character to its right operand's last), nothing else changed: `a < b == c` -> `(a < b) == c`; `a == b < c` -> `a == (b < c)`. |
| `E0109` | `'-> unit' is not written; omit the return type` | **Specified (r1 Phase B), not yet emitted - first emitted by the post-r1 #4 parser step.** An explicit `-> unit` return type. `unit` is a reserved word but not a writable type; "returns nothing" is spelled by omitting the annotation. | Replace the span from `->` through `unit` with the empty string (nothing is inserted). |

**Documented exceptions (r1 Phase B) - generic `E0105`, no dedicated code and no fix hint, by design:** a reserved word (`u8`..`i64`, `bool`, `unit`) where an identifier is required; `unit` in any type position other than after `->` (that one site gets `E0109`); an empty array literal `[]`; a repeat count that isn't an integer literal (`[v; X]`); conversion arity/shape (`u8()`, `u8(a, b)`, a bare `u8` in expression position). Each is a plain expected/found mismatch already fully described by `diag_expected`'s template; a rename or a different construct isn't a single-span mechanical edit. Rationale and exact wordings: docs/M2_PLAN.md "Diagnostics added by r1 Phase B".

IR-stage diagnostics implied by the same spec (struct-construction field checks, array-literal element types, conversion operand/target types, lvalue validity, ...) are listed in that section without codes; the `E02xx` range is assigned when the IR stage starts.
