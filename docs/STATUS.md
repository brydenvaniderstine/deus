# Status

Updated at every tag (`docs/README.md`'s consistency check runs at the same time). Open risks are tracked in `CLAUDE.md` ("Open risks") and `VISION.md` ("Candidate directions") - not duplicated here, just pointed to.

## Current milestone

**M2: the Manna compiler**, host-side on macOS (`docs/M2_PLAN.md`). Spec frozen at tag `m2-spec`; now implementing the parser (`tools/mannac/`).

## Done

- **M1** (tag `m1-boot`): Limine boot, GDT/IDT, 3-way paging (1 GiB identity/HHDM + 4 KiB kernel-image alias), PIT-calibrated LAPIC preemption, W^X+NX enforced and regression-tested (`make test-wx`). All Milestone 1 acceptance criteria verified against real `serial.log`/`qemu.log` output.
- **M2 spec** (tag `m2-spec`): v0 language spec and grammar, the lexer (`tools/mannac/lexer.c`, 14 golden tests, plain + ASan/UBSan), `docs/MANNA_PRINCIPLES.md`, and the full set of v0 language design decisions (file header, naming, `.mn` extension, error JSON schema, AST IDs, `manna` tooling binary).
- **Parser Step 1**: arena allocator - hard cap enforced via `longjmp`, zero allocation past the cap, documented ASan intra-arena-overflow limitation. `make test-arena`/`-asan`.
- **Parser Step 2**: AST node types, constructors, S-expression pretty-printer - verified byte-for-byte against `M2_PLAN.md`'s own documented example. `make test-ast`/`-asan`.
- **Parser Step 3**: diagnostics infrastructure (`Diagnostic`/`DiagList`, the four dedicated messages) and the shared `ParserState` - depth counter, error cap, and one unified `longjmp` escape point for all three "abandon this file" conditions. `docs/error_codes.md` registry started (E01xx). `make test-parser-state`/`-asan`.
- **Parser Step 4**: primary + postfix expressions (`tools/mannac/parser.c`), `make test-parser`/`-asan` wired up for real against `tests/parser/*.mn` golden ASTs (24 cases, 100% line coverage on `parser.c`). Depth-bound hooks are in every parse function from this step onward. Added the generic `diag_expected` diagnostic (E0105) for every non-dedicated syntax error.
- **Parser Step 5**: full expression precedence chain (`logic_or` down to `unary`), the non-associative comparison/equality chain detection (E0101 fires for real now, including cross-level cases like `a == b == c`), bitwise-binds-tighter-than-comparison verified explicitly. Along the way, found and fixed a real miscalibration in the depth bound (redundant Step 4 hooks made paren/call/index nesting cost 3x what unary nesting did - see the separate "Remove redundant depth hooks" commit) and then empirically proved the fix: 255 levels of nested parens/unary/index/calls parse successfully, 256 and 300 all cleanly give E0103, uniformly across all four constructs, under both plain and ASan, zero crashes (`tests/parser/depth_*`). Mixed nesting (`!(!(!(...)))`) costs 2 depth units per level as expected, capping at 127/128. 75 golden tests total, 100% line coverage on `parser.c`.

## Next

Remaining parser steps (approved plan, `docs/M2_PLAN.md`'s working log / session history): simple statements (Step 6) → `postfix_stmt`/`expr_tail` dispatch + `if`/`while`/`for` (where the assignment-as-condition diagnostic gets exercised for real, Step 7) → top-level items (`struct`/`function`/`version_decl`, Step 8) → dedicated depth-bound tests (Step 9, now largely pre-empirically-proven for expressions by Step 5 - likely narrows to statement/block/type nesting once those exist) → dedicated error-recovery tests (Step 10) → determinism test (Step 11) → libFuzzer harness (Step 12).
