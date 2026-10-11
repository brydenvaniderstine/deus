# Deus — Vision

This document is forward-looking context, not a spec. `CLAUDE.md` is the source of truth for what's actually being built; this file holds the longer-range goals and the R&D leads we're tracking but haven't committed to.

## Cross-platform bridge

Deus's single-CR3, flat-paging, LBS-enforced design targets bare metal first. The same kernel should run as a guest under major hypervisors without redesigning the memory model — paging stays flat, context switches stay register-save-only, nothing here should need a second code path per hypervisor.

Two concrete guest environments, once Milestones 1–3 are stable enough to boot under a hypervisor at all:
- **KVM/virtio**: the common Linux-hypervisor case. virtio-net/virtio-blk as the first drivers, since they're the lowest-friction path to real I/O under KVM.
- **AWS Nitro (ENA + NVMe)**: the common cloud case. Nitro's ENA (networking) and NVMe (storage) are the actual hardware interfaces Nitro exposes to the guest — not virtio — so this is a second, separate driver surface, not a variant of the KVM path.

**Mandate: no guest-performance claim ships without a benchmark.** "Should be faster than Linux here" is not a result — only a measured comparison on the same host/instance type, against an equivalent Linux guest, is. This applies to every cloud/guest claim in this document and in CLAUDE.md going forward.

## Candidate directions (tracked, not committed)

These are R&D leads under consideration, not roadmap. None are assumed anywhere in the Milestone 1–4 plan in CLAUDE.md. Each moves from "candidate" to "planned" only after its validation step below produces an actual result — not before.

### Standard IR for accelerators (SPIR-V / MLIR)

Multi-vendor accelerator support (M4 territory) is more tractable if Deus targets a standard IR rather than writing a bespoke backend per vendor. SPIR-V and MLIR are both candidates; neither is chosen.

**Validates via:** a SPIR-V emission prototype — lower one trivial Manna kernel function to SPIR-V, confirm it round-trips through `spirv-val` and disassembles to sane IR. Until that prototype exists and is reviewed, no accelerator-interface code should assume SPIR-V/MLIR as the target IR.

### Tenstorrent as first native open-source hardware target

Tenstorrent ships an open driver/SDK stack, which makes it a plausible first real accelerator target for M4 — more inspectable than a closed vendor stack, so failures are debuggable instead of opaque.

**Validates via:** a driver spike — the simplest possible host-side interaction with a Tenstorrent device (enumerate it, read a register, or run their existing SDK's hello-world) on whatever hardware is available, reporting what the actual driver surface looks like. No Deus-side accelerator interface should be designed around Tenstorrent specifics before this spike exists.

### AI-generated AST input to the compiler

An LLM-based tool could emit a Manna AST directly for generated code, skipping the text lexer/parser. This is an attacker-adjacent surface whether or not that's the intent: a generator can be wrong or compromised the same way a human author can.

**Risk:** an AI-generated AST must never get a shortcut past verification. It has to pass through the exact same static pointer verifier and typed-IR checks (see the Milestone 2 blueprint) as hand-written, compiler-parsed source — no separate "trusted" ingestion path.

**Validates via:** a sandboxed AST parsing test — hand-construct a deliberately malformed or adversarial AST (the kind a buggy or adversarial generator might produce) and confirm the verifier rejects it exactly as it would reject the equivalent malicious hand-written source. No AST-ingestion path ships until this test exists and passes.

### Agent service on Deus

A user-space Wasm service that runs AI tasks under explicit, capability-limited permissions (Manna declared effects). Model output and fetched web content are untrusted data and can never trigger an action by themselves. The model never runs in the kernel; it is reached off-box (API) or, later, run as a local model. The kernel stays provider-neutral.

**Prerequisites:** M3 (Wasm runtime + IPC), networking, and the Spectre mitigation plan CLAUDE.md requires before networking.

**Validates via:** a demo where a task granted read-only permission attempts a write, is denied, and the denial is logged.

### AI search on Deus

A Deus app built on the agent service: query an external search API, return ranked results plus answers whose every claim links to a source passage, with conflicting sources shown, not hidden. No own web index.

**Prerequisites:** the agent service above has passed its validation.

**Validates via:** a query whose answer cites at least two sources, where every cited link resolves and the quoted passage is present on the page.

### Native browser on Deus

A minimal browser written in Manna, starting with a reading-mode renderer (HTML + basic CSS, no JavaScript), with the agent service as a sidebar. JavaScript, if ever, runs only as sandboxed Wasm-hosted code under LBS. Not a Chromium/WebKit port.

**Prerequisites:** networking, a graphical shell, and the AI search candidate.

**Validates via:** rendering a fixed set of static test pages to match reference screenshots, with fetched page content unable to invoke any agent action without user approval.

## "Better than Linux" scorecard

A standing discipline, not a marketing claim: every item below is either something we can already point to in this repo, or a target we haven't measured yet. The two categories are never allowed to blur together.

### Claims we can already demonstrate

Each bullet names the file or command that backs it. Last re-verified 2026-10-08 (r1 Phase C): `make test-wx` run, the greps below run, the parser/lexer goldens run via `make test-lexer`/`make test-parser`.

- **W^X + NX on the higher-half kernel-image mapping.** Per-section flags are set in `kernel/arch/x86/paging.c:199-201` (`.text` R+X, `.rodata` R+NX, `.data`/`.bss` RW+NX), with `EFER.NXE` enabled before the CR3 load and `CR0.WP` after - the serial log shows `[paging] EFER.NXE enabled`, then `CR3 loaded`, then `CR0.WP enabled`, in that order. `make test-wx` runs three deliberate violations and each takes the predicted `#PF`: writing `.text` -> error code `0x3`; executing `.data` -> `0x11`; executing `.text` through the identity alias -> `0x11`. **What the tests do not cover:** `.rodata` and `.bss` specifically, and - a real hole, tracked in `CLAUDE.md`'s open risks - the same physical bytes stay read/write/exec-capable through the 1 GiB identity/HHDM aliases, so W^X is enforced on the kernel-image alias only. `make test-wx` is run by hand; CI builds the kernel (`make all`) but does not run it.
- **One CR3 load, ever.** `kernel/arch/x86/paging.c:212` is the only call to `load_cr3`, and `paging.c:78` the only `mov ... %cr3` in `kernel/`; there is no `invlpg`/`invpcid` anywhere in `kernel/` (checked by grep, 2026-10-08). The evidence is source inspection, not a runtime counter, and the one boot-time CR3 load itself flushes the TLB once. Context switches (`switch.S`) save and restore registers only.
- **Out-of-range numeric literals are rejected at lex time, never wrapped.** `tests/lexer/err_overflow.mn` (decimal 2^64, hex `0x10000000000000000`, boundary `0xFFFFFFFFFFFFFFFF` accepted), golden `err_overflow.tokens`, run by `make test-lexer`. This is about literals only - not runtime arithmetic (below).
- **Two classic C traps are parse errors with a dedicated message.** `a < b < c` -> E0101 (`tests/parser/err_chained_same_cmp.mn`, `err_chained_same_eq.mn`, `err_chained_cross_cmp.mn`); `if x = 5 {` / `while x = 5 {` / a `for` condition `i = 5` -> E0102 with a `==` fix-it built in `tools/mannac/diag.c` (`err_if_assign_condition.mn`, `err_while_assign_condition.mn`, `err_recovery_for_cond_assignment.mn`). Run by `make test-parser`.

### Designed, not yet implemented

These were listed above as demonstrable before r1 Phase C; none has a test or a running implementation today, so they are design intent, not results. Each moves back up only with a cited test.

- **Bounds-checked slices (`arr[a..b]`).** The slice *syntax* parses (`tests/parser/block_let_slice_type.mn`), but nothing checks a bound: the checks live in the typed IR, static verifier and Wasm backend (`docs/M2_PLAN.md` §3), none of which exists yet.
- **No undefined behavior.** A principle (`docs/MANNA_PRINCIPLES.md` principle 4), not a property of anything running: there is no semantics beyond parsing to be defined or undefined.
- **Runtime overflow traps (no silent wraparound in arithmetic).** Specified (`docs/M2_PLAN.md` §2); needs IR and backend. Only the literal-range check above exists.

The bar for the first list is a file, a test, or a log line in this repo right now. The second list is not a claim and must not be quoted as one.

### Metrics to benchmark against Linux (identical QEMU config, both sides)

- **Context-switch latency** — the thing our zero-CR3-reload design is supposed to win on. Not benchmarked yet.
- **IPC round-trip time** — needs the IPC design CLAUDE.md's M3 phase calls for; doesn't exist yet.
- **Boot-to-shell time** — needs a shell; doesn't exist yet.
- **Memory footprint** — kernel + minimal userspace, measured, not estimated.

**Rule: no performance claim is made until benchmarked.** Not "should be faster," not "designed to be faster" presented as if it were a result — a number, measured on both sides under the same QEMU configuration, or no claim at all. This is the same discipline the cross-platform bridge section above already commits to for cloud-guest performance; this scorecard is where it applies to the bare-metal vs. Linux comparison specifically.

**Benchmarks run no earlier than post-M3** — IPC and a shell are both prerequisites for every metric above except memory footprint. Until M3 lands, every item in this metrics list is a target, not a result, and must be presented as one.

## Non-goals (for now)

- No commitment to any specific accelerator vendor, IR standard, or cloud provider beyond what's named above as a tracked candidate.
- No guest-hypervisor work starts before Milestones 1–3 (boot, Manna compiler, Wasm runtime) are stable on bare metal.
- No AI-AST ingestion path exists yet; nothing in the compiler assumes one.
- No port of an existing browser engine (Chromium/CEF/WebKit) to Deus: its size, POSIX dependence and JIT conflict with the kernel budget, W^X and the single-address-space design.
- No English-like or indentation-based Manna syntax: it breaks the brace grammar guarantees the parser relies on (M2_PLAN.md).
- No LLM call as language syntax (e.g. `ask_model`): model access is a library call behind a declared effect, keeping builds deterministic (MANNA_PRINCIPLES.md principle 12).
- None of the three candidates above is assumed anywhere in M2-M4.
