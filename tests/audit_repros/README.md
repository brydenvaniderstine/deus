# m2-ir-v0 audit repros

One input per audit finding (the first audit: `~/Desktop/m2-ir-v0-audit-findings-2026-10-21.md`, inputs `01_` ... `09_`; the re-audit of step 17: inputs `R2_*`, the auditor's files verbatim), with the output the code gives **after step 18** (the first audit's outputs did not change between step 17 and step 18).
For `NN_name.mn`:

- `NN_name.out`: `ir_dump NN_name.mn`
- `NN_name.fixed.mn`: `ir_dump --fixed NN_name.mn` (the source with every fix-it applied)
- `NN_name.fixed.out`: `ir_dump NN_name.fixed.mn` (the re-check)

Re-run them all: `tools/run_audit_repros.sh <path-to-ir_dump>` (build `ir_dump` as in `BUILD.txt`). Regenerate (after reading the diff!): `tools/make_audit_repros.sh <ir_dump>`.
These files are not part of `make test-*`; they exist for the re-audit. The same inputs are also goldens in `tests/ir` and `tests/ir_fix`.

| # | Input | Expected now (what the finding said before) |
|---|---|---|
| 1 | `01_*` | exactly one E0222 at 3:12 (was two: 3:12 and 3:29) |
| 2 | `02_*` | E0270 twice, then E0235 twice: calls inside a bad assignment target still count toward recursion (was E0270 only) |
| 3 | `03_*` | E0257 at the declaration, E0256 at the bare `len` after the scope closed (finding 3: the spec text was changed to say "visible declaration"; the behavior is the same) |
| 4 | `04_*`, `04b_*` | `04`: no fix offered where no lossless widen gives the expected type (fixed source == input; the re-check lists the same six E0261). `04b`: each fix widens to the expected type and the fixed source builds clean (was: a narrowing wrap, then E0223) |
| 5 | `05_*` | one E0261 and **no fix** (fixed source == input) since step 19 (19c-5): the wrap would make the 511-operation chain 513 high. Before: a fix whose re-check reported E0111 at 3:2068 (it was a recorded known issue) |
| 6 | `06_*` | the `fix` renames both `myVar` to `my_var`; the re-check reports only the real E0213 (was E0280 with no fix, forever) |
| 7 | `07_*` | both `Idx` loops get a fix to `idx`; the fixed source builds clean (was: only the first) |
| 8a | `08a_*` | the use is renamed with the declaration; the re-check reports E0231 on `do_it` (was E0230, an unknown name) |
| 8b | `08b_*` | the second `aB` in the construction is renamed too; the re-check reports E0244 on `a_b` (was E0242) |
| 9 | `09_*` | one E0222 at 3:12 from the outer multiplication of `[281474976710656][65536]u8` (a test gap; behavior was already right) |
| 10 | n/a | `test_e0261_fix` in `tools/mannac/ir_test.c` now re-checks every fixed source: `make test-ir-unit` |
| 11 | n/a | `make audit-ir-poison`: `ir_rename.c` is in the file list, 106/106 lines run by a one-diagnostic golden |
| 12 | n/a | disclosure only (the brief): the ungated fuzz statistics came from an untracked program |

## Re-audit of step 17 (inputs `R2_*.mn`; fixed in step 18)

| Input | Expected now (what the re-audit found before) |
|---|---|
| `R2_F1a`, `F1c`, `F1d`, `F1e` | one E0261 and **no fix** (fixed source == input). Before: a nested call argument, construction field or array element overwrote the expected type, so a lossy or narrowing wrap was offered and the fixed source failed |
| `R2_F1b` | one E0261 (`u8` and `i8`) and no fix: neither direction is lossless. Before: a lossy `u8(...)` wrap |
| `R2_F4a`, `F4b` | one E0261 and no fix: the wrap would make the enclosing `+` (or the unary `-`'s place) fail. Before: a fix whose result was not clean |
| `R2_F2a`, `F2b` | the rename keeps the parentheses: `(my_var) = 2`, `(my_p).x = 2`, `(my_p)[0] = 3`; the re-check reports the same E0270s. Before: the parentheses were deleted and the E0270 vanished |
| `R2_F3a`, `F3b` | E0280 and E0231, **no fix** (fixed source == input): the rename would have made a local capture the function used as a value, or the function a local. Before: a fix that changed what `helper_fn` meant |
| `R2_d1` | E0280 and E0230, no fix: renaming `itemCount` would make the unknown `item_count` resolve to it. Before: it resolved after the fix |
| `R2_f1`, `f2` | the mute across loops, branches and the 20-error cap: `R2_f1` ends `E0270`, `E0270`, `E0203` after 18 `E0223`; `R2_f2` prints the E0270s and the value-side errors (`nope2`, `h()`) but nothing from inside a target. Sound before and after |
| `R2_g1` | three E0222 (one per oversized annotation) and the one independent E0261 (`take(..)` is `u8`, `len(..)` is `u64`). Sound before and after |
