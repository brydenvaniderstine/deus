# m2-ir-v0 audit repros

One input per audit finding (`~/Desktop/m2-ir-v0-audit-findings-2026-10-21.md`), with the output the code gives **after step 17**.
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
| 5 | `05_*` | **known issue, not fixed**: the fix on a 511-operation chain makes it 512 high; the re-check reports E0111 at 3:2068. Recorded in `docs/STATUS.md` Known issues |
| 6 | `06_*` | the `fix` renames both `myVar` to `my_var`; the re-check reports only the real E0213 (was E0280 with no fix, forever) |
| 7 | `07_*` | both `Idx` loops get a fix to `idx`; the fixed source builds clean (was: only the first) |
| 8a | `08a_*` | the use is renamed with the declaration; the re-check reports E0231 on `do_it` (was E0230, an unknown name) |
| 8b | `08b_*` | the second `aB` in the construction is renamed too; the re-check reports E0244 on `a_b` (was E0242) |
| 9 | `09_*` | one E0222 at 3:12 from the outer multiplication of `[281474976710656][65536]u8` (a test gap; behavior was already right) |
| 10 | n/a | `test_e0261_fix` in `tools/mannac/ir_test.c` now re-checks every fixed source: `make test-ir-unit` |
| 11 | n/a | `make audit-ir-poison`: `ir_rename.c` is in the file list, 106/106 lines run by a one-diagnostic golden |
| 12 | n/a | disclosure only (the brief): the ungated fuzz statistics came from an untracked program |
