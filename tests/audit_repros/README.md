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
| `R2_F2a`, `F2b` | the rename keeps the parentheses: `(my_var) = 2`, `(my_p).x = 2`, `(my_p)[0] = 3`; the re-check reports the same E0270s (and, since C11, `(my_p)[0]` also reports E0252, indexing a struct, a mistake of its own). Before: the parentheses were deleted and the E0270 vanished |
| `R2_F3a`, `F3b` | E0280 and E0231, **no fix** (fixed source == input): the rename would have made a local capture the function used as a value, or the function a local. Before: a fix that changed what `helper_fn` meant |
| `R2_d1` | E0280 and E0230, no fix: renaming `itemCount` would make the unknown `item_count` resolve to it. Before: it resolved after the fix |
| `R2_f1`, `f2` | a bad assignment target across loops, branches and the 20-error cap. Since C11 (Bryden 2026-10-27) a target is checked with diagnostics ON: `R2_f1` now ends `E0270`, `E0233` (the arity of the call in the target), `E0203` at the FIRST target after 18 `E0223` (before: `E0270` at both targets, then `E0203`: the second target's E0270 is no longer reached); `R2_f2` prints the E0270s, the value-side errors (`nope2`, `h()`) and now the mistakes inside the targets (`nope`, the `true` argument, the arity of `g(1, 2)`, the field of its u8 result). Sound before and after |
| `R2_g1` | three E0222 (one per oversized annotation) and the one independent E0261 (`take(..)` is `u8`, `len(..)` is `u64`). Sound before and after |

## Re-audit 3 of e45c0bc (inputs `R3_*.mn`, the auditor's files verbatim; fixed in step 19)

| Input | Expected now (what the audit found before) |
|---|---|
| `R3_mem_min` (+ `R3_mem_min_snake_control`) | Before C11: E0280 x2 and E0270 only, both renames offered (the 300 uses of `zz` in the bad target were silent); before step 19: a spurious E0201 (the rename bookkeeping converted every misnamed name again per use). Since C11 every use in the target is reported: E0280 x2, E0270, E0232, and 16 of the 300 E0230 until the 20-error cap (E0203) stops the build, which offers no fix (register C10), so `--fixed` is the input unchanged. The memory property (no E0201, linear arena) is held by `make test-ir-memory-stress`, whose cases keep their shape under the cap; the auditor's 1.1 MB `mem_fn.mn` is not committed |
| `R3_mem_min_valid_uses` | The audit's `mem_min` with its bulk kept through valid uses (step 21i, D3): two misnamed 2,000-character locals and 300 calls of a well-named `g`. E0280 x2 and nothing else, both renames offered, the fixed source checks clean. Sound on e45c0bc too: with valid uses the old per-use conversion is never reached (it ran only for a name that was not a local, which is always an error, and the 20-error cap bounds those) |
| `R3_k1`..`k4` | the E0261 fix keeps a space after the keyword: `return i32(-a) + b;`, `return u32((a)) + b;`, `if u32((a)) < b {`, `while u32((a)) < b {`, and the fixed sources check clean. Before: `returni32(-a)` etc. (a different program, E0232 / E0105) |
| `R3_t1`..`t3` | one E0261 and **no fix**: the new type would become the local's type and a later use rejects it (`return r` as `u8`, `i < n` with `n: u8`, an argument `x: u8`). The fix waits for the local's uses and is withdrawn by the first that rejects it. Before: a fix whose re-check reported E0223 / E0261 |
| `R3_g1`, `R3_g2` | one E0261 and no fix: the literal beside it (`70000`, `-1`) does not fit the new type (`u16`, `u32`). Before: a fix whose re-check reported E0224 / E0260 |
| `R3_cap0`, `R3_cap1` | one E0261 (+ E0230 x1, or x19 and E0203) and no fix in either. Before: `cap1` kept a fix the parent would have withdrawn |
| `R3_r1`, `R3_r2` | the rename is offered and reaches the stale spelling: `let q: my_fn = 1;` (still E0220), `P(1, my_field: 2)` (still E0240). Before: r1 was refused, r2 left `myField:` behind |
| `R3_r3`, `R3_r4` | the rename is refused (a real collision: `my_field:` is written where it resolves to nothing and would start to resolve): the source is returned unchanged, E0280 + E0240 / E0242 |
| `R3_mu1`..`mu4` | one E0261 and **no fix** in a position that does not hand its place to what is inside it: the first element of an un-annotated array (`let r = [a + b, c];`), a `len` argument, a field base, under `!`. The auditor's mutants 2 and 3 (re-audit 3, findings 7 and 8) gave a fix whose re-check reported E0223 / E0255 / E0251 / E0260. See the mutant table in the step 19 report |
