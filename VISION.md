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

## "Better than Linux" scorecard

A standing discipline, not a marketing claim: every item below is either something we can already point to in this repo, or a target we haven't measured yet. The two categories are never allowed to blur together.

### Claims we can already demonstrate

- **Memory safety via bounds-checked slices.** `arr[a..b]` carries its own bounds; the verifier proves accesses in range where it can and traps at runtime where it can't (docs/M2_PLAN.md).
- **No undefined behavior.** Every Manna program has exactly one meaning, fails to compile, or traps — by design (docs/MANNA_PRINCIPLES.md principle 4), not by convention.
- **Overflow traps, no silent wraparound.** Verified this session for both decimal and hex literals at the exact `2^64-1` boundary (`tests/lexer/err_overflow.mn`).
- **W^X + NX enforced on the kernel image.** `.text` is R+X, `.rodata` is R+NX, `.data`/`.bss` is RW+NX, `EFER.NXE` set before the CR3 load, `CR0.WP` set after — verified via three deliberate-fault regression tests (`make test-wx`), each producing the exact predicted `#PF` error code.
- **No CR3 reloads.** One CR3, loaded once, for the lifetime of the kernel (`kernel/arch/x86/paging.c`) — zero TLB flushes from context switching, by construction, not by tuning.

Every one of these has a file, a test, or a log line backing it in this repo right now. That's the bar for staying in this list.

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
