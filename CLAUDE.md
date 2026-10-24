# Deus

An infrastructure OS for the 2030s, targeting cloud infrastructure, high-performance edge devices, and laptops. It replaces the monolithic Linux paradigm and its costs: ring transitions, CR3 switches, and TLB churn.

The spiritual baseline is TempleOS/ZealOS, for simplicity and a unified JIT/compiler shell. We remove TempleOS's flaws — cooperative multitasking, no security, no networking — and add Language-Based Security (LBS) plus an asynchronous microkernel. Isolation is enforced by the compiler, not the MMU/hardware rings.

You are lead co-developer. Build incrementally. Every milestone must boot in QEMU.

## Hard constraints (do not violate)
- **x86_64 only, for now.** ARM/mobile is out of scope.
- **Paging stays on.** Long mode requires CR0.PG.
  - Build page tables once. Load CR3 once and never reload it. Zero TLB flushes, near-zero misses, no syscall ring transitions.
  - 1 GiB pages for the identity map and the HHDM alias - both are naturally 1 GiB-aligned by construction. 4 KiB pages for the kernel-image alias: `virtual_base` is architecturally fixed at a 1 GiB-aligned address (0 offset within its frame), but Limine's `physical_base` is only ever guaranteed page-aligned, not 1 GiB - a 1 GiB page can only alias the two when their low 30 bits happen to match, which they generally won't. (Found the hard way: Milestone 1 silently aliased the kernel to the wrong physical frame this way - no fault, just undefined execution after the CR3 load.)
  - Context switches still save and restore registers (see `switch.S`/`sched.c`) — only the CR3 load is eliminated, not the register-save cost.
- **Three mappings live in that single CR3:**
  1. An identity map of all RAM plus the MMIO holes — the 3–4 GiB hole (LAPIC at 0xFEE00000, IOAPIC, HPET) and any 64-bit PCI BARs.
  2. A higher-half alias of the kernel's physical load region, at `0xffffffff80000000` (Limine requires this link address).
  3. The HHDM range Limine reported, aliased to the same physical memory. This keeps Limine response pointers and the boot stack valid after the CR3 load — without it, the first HHDM-addressed access after the switch triple-faults.
- **Never hardcode physical placement.** Read the Limine memmap response, then place the regions below around reserved/firmware areas.
- **MMIO caching.** The LAPIC is at 0xFEE00000 (confirm via MSR 0x1B), and PCI BARs may sit above 4 GiB.
  - Rely on firmware MTRRs for UC, or set PCD/PWT on MMIO pages. Document the choice.
- **Preemption** comes from the LAPIC timer. Cooperative-only scheduling is banned.
  - Timer mode: use TSC-deadline when supported (`CPUID.01H:ECX[24]`) and the TSC frequency is known; otherwise LAPIC periodic mode. TSC-deadline is a timer *mode*, not a calibration source — it still needs a calibrated frequency from the cascade below.
- **Compiler flags:** freestanding, `-mno-red-zone`, `-mcmodel=kernel`, `-mgeneral-regs-only`.
  - The last one avoids saving FPU/SSE state in ISRs until we add XSAVE.

## No degradation (hard rule, Bryden 2026-10-23)

Progress never buys a weaker product. This rule outranks every approval tier below.
- Nothing is stubbed, removed, withheld, weakened, or labelled "accepted", "documented exception", "known issue", "stub" or "planned, not built" in order to make progress. Each such item needs Bryden's explicit OK and an entry in `docs/DEGRADATION.md` with its reason, the promise it breaks (a `file:line` quote), an owner and a hard revisit trigger (a tag or milestone).
- No tag or milestone closes while any register entry's trigger has passed.
- A milestone closes only on its full spec. A stub left at close is a register entry Bryden approved.
- A test, golden, fix case, repro or diagnostic count never decreases unless the register lists why. (Enforced by CI from step 20; by review until then.)
- Every retro rule lives in a check, or sits in the register as "unenforced" with a trigger.
- Silent fallbacks are banned: a self-check failure is a hard failure in tests, CI and fuzzing, and a loud internal error for users, never a silent drop.
- The batch cap (at most 3 steps, then STOP; see "Working conventions") is enforced.
- Neither Claude Code nor the reviewer offers "turn off", "withhold" or "accept" as a way to finish. If such an option is mentioned, it is labelled "degradation" and the full fix is written next to it.

**Fix-it policy (policy C, Bryden 2026-10-23).** Every diagnostic gets a machine-applicable fix, or a short list of fix choices (the JSON `"fixes"` array, `docs/MANNA_PRINCIPLES.md` principle 6). A fix never guesses, and every offered fix must re-check clean. The only exceptions are the safety limits E0104, E0110, E0111, E0201, E0203 and E0222, each listed in `docs/DEGRADATION.md` section 7a. The current "None - documented exception" entries in `docs/error_codes.md` stay for now, each a register entry (section 7b) triggered by the post-m2-ir-v0 fix-it pass.

## LBS and the language
- Isolation is enforced by the compiler, not the MMU.
- **Manna** descends from HolyC and has two compile targets:
  - **User apps** compile to WebAssembly and run in a bare-metal Wasm runtime. Budget a 5–20% bounds-check overhead without guard pages.
  - **Kernel code** compiles to native machine code. It must pass a static verifier and carry a `[kernel_hook]` attribute to use raw pointers.
- All Manna language changes must pass the Feature Admission Test in `docs/MANNA_PRINCIPLES.md`.

## Bootstrap path
- The bootstrap kernel is written in x86_64 assembly and freestanding C, and hosts the resident Manna compiler.
- Self-hosting is the goal: the kernel gets rewritten in Manna.
- A minimal assembly core stays **permanently**: entry, ISR stubs, the context switch, and the CR3 load. This never gets rewritten in Manna.
- Keep the C bootstrap buildable, for reproducibility and to cross-check the compiler (diverse double-compiling).

## DMA protection
- Implement an IOMMU (Intel VT-d first, AMD-Vi later) before running any untrusted driver.
- Without it, DMA bypasses LBS entirely.

## Clocks and preemption (calibration cascade)
- Sources, in order: **CPUID 0x15/0x16 → HPET → PIT**. Log which source was used, every boot.
- CPUID 0x15/0x16 needs no timing loop at all when enumerated, but qemu64 doesn't enumerate it — expect PIT to win on this dev target until real hardware or a better QEMU CPU model is in play.
- TSC-deadline is a timer *mode* (see Hard constraints), not a calibration source — don't conflate the two.

## Target memory regions (logical; physical placement per memmap)
| Range | Purpose |
|---|---|
| 0x0 – 0x100000 | Reserved (firmware, Limine handoff) |
| 0x100000 – 0x3FFFFFF | Microkernel text/data |
| 0x4000000 – 0x7FFFFFF | Telemetry rings, JIT profiling tables |
| 0x8000000 – end of RAM | Shared heap, linear Wasm sandboxes |

If the memmap conflicts with these ranges, log it over serial. Then relocate the region. Do not overwrite firmware memory.

## Scope
- x86_64-first. ARM is deferred.
- ASIC/accelerator routing is long-term R&D. No architecture commitments yet.
- Cloud note: under a hypervisor, nested paging (EPT/NPT) still adds 2D walks on top of our flat CR3. Plan virtio drivers.

## Goals
These are targets, not hard constraints (see above) - aspirational, not enforced by any build check today.
- **Kernel scope: single-person-comprehensible, ~120k LOC max.** Kernel only - this budget excludes `mannac` (the Manna compiler, `tools/mannac/`) and the Wasm runtime (M3). If the kernel itself needs more than ~120k LOC to do its job, something has grown beyond what one person can hold in their head, which defeats the point of a small, auditable TCB.

## Toolchain & run
- Compiler: `clang --target=x86_64-elf` (or `x86_64-elf-gcc`) plus `ld.lld`.
- Bootloader: Limine (UEFI). Use the `limine.h` protocol with a base revision tag. Vendored (gitignored, not tracked) at pinned tag `v8.7.0-binary` (commit `aad3edd370955449717a334f0289dee10e2c5f01`) - `make all` fetches and verifies it automatically via `tools/fetch-limine.sh`, which fails loudly on a hash mismatch rather than building against an unverified checkout.
- **Dev boot** (no disk image needed — QEMU's vvfat driver serves a staged directory directly):
  ```
  qemu-system-x86_64 -machine q35 -m 8G -cpu qemu64,+apic,+pdpe1gb \
    -bios /path/to/OVMF.fd -drive format=raw,file=fat:rw:build/esp \
    -serial file:serial.log -monitor stdio -no-reboot -d int,cpu_reset -D qemu.log
  ```
  - For IOMMU milestones, add `-device intel-iommu,intremap=on` and `-machine q35,kernel-irqchip=split`.
- **Release builds** use a GPT image instead: a FAT32 ESP holding `EFI/BOOT/BOOTX64.EFI`, `limine.conf`, and the kernel ELF, booted with `-drive format=raw,file=deus_disk.img` (`make image`, via `tools/make_image.sh`).
- `+pdpe1gb` is mandatory, because qemu64 lacks 1 GiB page support by default.
- Provide a `Makefile` with these targets: `all`, `image`, `run`, `debug` (adds `-s -S`), and `clean`.

## Repo layout (proposed)
```
kernel/
  boot/      entry.c, limine requests, linker.ld
  arch/x86/  gdt.c, idt.c, isr.S, paging.c, lapic.c, pit.c, clock.c, serial.c
  sched/     sched.c, switch.S
  lib/       minimal string/printf
tools/       image build scripts
```

## Existing code (from design session)
- `switch.S` is the LAPIC timer ISR. It pushes 15 GPRs and calls `sched_tick(rsp)`. It loads the returned rsp and does `iretq`.
  - Stack is 16-byte aligned at the call.
- `sched.c` contains:
  - the frame layout and a round-robin `sched_tick`;
  - the telemetry ring write (RIP samples);
  - `task_init`, which builds a fake iretq frame;
  - `lapic_timer_init`.
- Selectors `KCODE_SEL=0x08` and `KDATA_SEL=0x10` must match the GDT you write.
- The LAPIC timer count is derived from `clock_calibrate_lapic_ticks_per_ms()` (the cascade above), not hardcoded.

## Milestone 1 — boot foundation (DO THIS FIRST)
1. **Limine stub:** Implement the base revision plus memmap, HHDM, and kernel-address requests. Add a serial (COM1) logger. Halt cleanly on a missing response.
2. **GDT:** null, kernel code 64, kernel data. Add a TSS with an IST stack for #DF. Reload the segments after `lgdt`.
3. **IDT:** 256 entries.
   - Exceptions 0–31 dump the vector, error code, RIP, and CR2 to serial, then halt.
   - Vector 0x20 is the timer; 0xFF is spurious.
4. **Paging:**
   - Detect max phys RAM from the memmap. Build the PML4/PDPT with 1 GiB pages covering RAM plus the 3–4 GiB MMIO hole.
   - Add the higher-half alias for the kernel, and the HHDM alias (see Hard constraints — three mappings, one CR3). Load CR3 once.
   - Verify: kernel survives the switch, and an identity access to a high RAM address works.
5. **Clock calibration:** cascade CPUID 0x15/0x16 → HPET → PIT (see above). PIT ch2: ~10 ms one-shot, count LAPIC ticks (div 16) over that window, derive the count per 1 ms tick. Log the source used.
6. **Preemption proof:** Two kernel tasks print 'A'/'B' to serial in loops with no yields. The interleaving output proves preemption.

**Acceptance:** `make run` boots. Serial log shows the memmap, the CR3 load, the calibration source and tick rate, and interleaved A/B output. No triple faults.

## Open risks (track every milestone; don't solve in M1)
- **Spectre:** a single address space plus LBS leaks across sandboxes. A mitigation plan is required before networking. Candidate mitigations, not yet validated: compiler-emitted LFENCE serialization (at verifier-identified speculation gadgets - bounds checks being the obvious case) and coarsened timers inside sandboxes (denying the precision a timing side-channel needs). Neither is implemented or proven sufficient; both need real measurement before being trusted.
- **TCB:** the compiler, verifier, and Wasm runtime all run in ring 0. One bug compromises everything. Fuzz them continuously.
- **DMA bypasses LBS.** An IOMMU (VT-d) is required before untrusted drivers (see DMA protection above).
- **Wasm bounds checks:** without guard pages, expect a 5–20% overhead.
- **Cloud/EPT:** nested paging under a hypervisor adds 2D walks our flat-CR3 design doesn't see on bare metal (see Scope above).
- **High 64-bit PCI BAR window unmapped:** QEMU's q35 reserves a sparse MMIO placeholder up near 1 TiB (seen at boot: base=0xfd00000000, length=0x300000000). We deliberately exclude it from the identity map (it's RESERVED, not RAM, and would need 1024+ 1 GiB pages in one PDPT). Mapping it for real needs a second PML4/PDPT entry - not needed by any M1 code path, but required before real PCI/virtio driver work.
- **CPUID 0x15/0x16 calibration is a stub.** qemu64 doesn't enumerate leaf 0x15 at all (`clock.c` logs and skips); even when enumerated on real hardware, converting crystal Hz to LAPIC ticks/ms isn't implemented.
- **HPET calibration is a stub.** Needs an ACPI table walk (RSDP → XSDT/RSDT → HPET table) to find its MMIO base, which doesn't exist yet; `clock.c` logs and skips.
- **TSC-deadline timer mode isn't implemented.** Milestone 1 only uses LAPIC periodic mode. The hard constraint above documents TSC-deadline as the preferred mode when `CPUID.01H:ECX[24]` is set and the TSC frequency is known - that detection and switch-over doesn't exist yet.
- **Kernel `.text` is writable via identity/HHDM aliases (1 GiB RW pages).** W^X is enforced on the higher-half kernel-image mapping (4 KiB pages, per-section flags), but the same physical bytes are also reachable - read/write/exec before the NX addition below, read/write after it - through the identity map and HHDM alias, which use single 1 GiB pages with no sub-granularity to split W from X. Closing this requires splitting the kernel's physical range out of those 1 GiB pages. Revisit before untrusted code runs.

## Later phases
- M2: the Manna compiler, `tools/mannac/` (design: `docs/M2_PLAN.md`) - a from-scratch, host-side C implementation (macOS), **not a ZealC port**. Built so far: the lexer and a recursive-descent parser with panic-mode error recovery (latest tag `m2-parser-v0.1`: the v0 grammar is complete and externally audited; v0.1 adds name spans and the E0111 tree-height bound). Not built yet, in pipeline order: typed IR / type checker, loop-counter injection, the static pointer-safety verifier, and the Wasm backend. Current state and next step: `docs/STATUS.md`.
- M3: bare-metal Wasm runtime (WAMR) and zero-copy IPC queues.
- M4: telemetry-driven JIT and accelerator interfaces.

See `VISION.md` for longer-range cross-platform/cloud-guest goals and R&D candidates (not committed roadmap).

## Working conventions
- Small commits, one subsystem each. Run `make run` before claiming success.
- Read `serial.log` and `qemu.log` to verify. Don't assume.
- Ask before changing any hard constraint above.
- Write commit messages via a quoted heredoc (`git commit -F - <<'EOF' ... EOF`), never `git commit -m "..."` for anything containing backticks or `$`. An unquoted `-m` string is still subject to shell expansion - a backtick-wrapped code span like `` `manna` `` gets executed as a command substitution and silently vanishes (empty output) if that command doesn't exist. Lost a word this way once; the quoted-heredoc form passes the message through literally, no expansion at all.
- **Approval tiers**, for incremental multi-step work (e.g. the parser build-out):
  - **ROUTINE** - tests pass, no design choice made, no spec deviation: commit locally and continue without stopping for approval on that single step. **A batch is at most 3 steps** - present them together for review once that cap is reached, never more at a time. **No push until the batch itself is approved** - routine steps accumulate as local commits, not as things that reach the remote unreviewed.
  - **STOP** - a design choice, a spec deviation, a failing test, or anything touching a hard-constraint area: stop immediately, before continuing to the next step, regardless of batch position.
  - Every step report includes a plain-English summary: what changed, why, the risk level (low/medium/high), and which tests prove it - not just a diff dump.
  - **Rule: no behavior change without a test.** If a change has no test demonstrating it, that's by itself a reason to treat the step as STOP, not ROUTINE.
- **At every tag**, list uncovered lines in `lexer.c`, `parser.c` and the typed-IR files `ir*.c` once they exist (`make coverage`); each must be justified as untestable or get a test. No percentage targets. The arena's abort path (`arena_test --no-target-overflow`) runs out-of-process and is expected to show as uncovered.
- At every tag, run the refinement loop in docs/REFINEMENT.md before starting the next stage.

## Session start ritual

Before starting any work in a new session, in order: read this file (`CLAUDE.md`), `docs/README.md`, `docs/STATUS.md`, `docs/DEGRADATION.md` (the register: which promises are open and when each comes due), and `docs/history/R1_TRIAGE.md` if `docs/STATUS.md`'s "Active work" line points to it (or whatever working doc a future "Active work" line names - the ritual is "read what's currently active," not hardcoded to this one filename forever). Then run `git status` and `git log -10` to see the actual current tree state, not just what the docs claim it is. Report back: what state the repo and the active work are in, and the exact next action you'd take - then **wait for explicit OK before doing anything**, even a ROUTINE-tier step. This exists because a fresh session has no memory of what was mid-flight; skipping it risks re-deciding something already settled, duplicating work, or acting on stale assumptions about what's done.

## Lessons

- **Proof over reasoning, always - run the actual limit under ASan, don't compute it.** Every depth-bound/recovery claim this project has made was verified by constructing a real input, running a real build, and reading the real output - never "derived" and trusted on its own. Caught several real bugs this way that pure reasoning missed (redundant depth hooks, brace-depth-unaware recovery, a recovery mechanism that didn't actually fix the bug it was built for). Applies to new work the same way: if a change has a specific numeric/behavioral claim, prove it with a built binary before writing it down as fact.
- **Grammar guarantees underpin recovery - re-verify them before reusing them.** E.g. "headers never contain `{`, so stopping at `{` during recovery is always safe" is true only because the v0 grammar happens to have that shape. A language change that breaks a guarantee like this silently breaks whatever recovery logic was built on it - check `tools/mannac/parser.c`'s own comments for "the grammar guarantees" before changing grammar that recovery code depends on.
- **Any new recursive parse function needs the depth invariant checked, not assumed.** The question is never "does this function have a depth hook" - it's "does every cycle in the call graph pass through at least one counted function." See parser.c's own INVARIANT comment for the full reasoning; re-read it before adding a new recursive production.
- **Quote-check doc changes against the actual repo, not memory.** Several real, pre-existing doc contradictions this project has found (a stale test count, a dangling reference to a section that didn't exist) were caught only by re-reading the doc against `git log`/`git ls-tree`/the actual current file, not by re-reading the doc in isolation. `docs/README.md`'s consistency check exists for exactly this; don't skip it by assuming a doc is still accurate because it was accurate when written.
- **Verify CI actually runs the tests you think it does, every time you add one.** A new test target only protects anything if it's wired into `.github/workflows/ci.yml` AND the push actually triggers a run (check `paths-ignore` doesn't accidentally skip it) AND the run is watched to green, not just pushed and assumed. Don't report a step done on the strength of local test output alone.
- **Prefer a prebuilt binary over building a toolchain from source via brew**, on this machine specifically: it's an unbottled (Tier 3) target, so `brew install` for anything with a deep dependency graph (e.g. a Rust or LLVM toolchain) can mean hours of from-source compilation instead of a download. Check for a prebuilt release asset first (`gh` itself and `wasmtime` were both fetched this way); fall back to brew only when nothing prebuilt exists for this platform, as was the case for `llvm`/libFuzzer.
- **A golden that loses diagnostics or AST nodes in a commit is STOP-tier: the report must list every removed line and why.** da9f01c re-baselined err_recovery_stress_no_infinite_loop from 7 diagnostics to 1 to match a regression, and CI stayed green.
