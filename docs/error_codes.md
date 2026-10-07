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
| `E0103` | `maximum nesting depth (256) exceeded` | A recursive parse path (expression, statement, block, type, unary self-recursion, or nested array types) went past `MAX_NESTING_DEPTH`. | None — the input needs restructuring, not a mechanical edit. |
| `E0104` | `too many errors, stopping` | Error count reached `MAX_ERRORS` (20) in one file; parsing stopped early as a safety valve against cascading noise. | None — fix the earlier errors first. |
| `E0105` | `expected X, found Y` (template) | Generic syntax error — every parse failure that isn't one of E0101/E0102 above goes through this one constructor (`diag_expected`, `tools/mannac/diag.c`), so the message format lives in one place. `X`/`Y` vary per call site (e.g. `expected ')' after expression, found '}'`). | Varies by call site; most have none yet. |
