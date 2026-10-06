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

## Next

**Parser Step 4**: primary + postfix expressions, with `make test-parser` wired up for real output for the first time. Depth-bound and error-recovery hooks go into every parse function from this step onward, per the approved plan - not retrofitted later.

Remaining steps after that (approved plan, `docs/M2_PLAN.md`'s working log / session history): full expression precedence chain → simple statements → `postfix_stmt`/`expr_tail` dispatch + `if`/`while`/`for` (where the two dedicated diagnostics get exercised for real) → top-level items (`struct`/`function`/`version_decl`) → dedicated depth-bound tests → dedicated error-recovery tests → determinism test → libFuzzer harness.
