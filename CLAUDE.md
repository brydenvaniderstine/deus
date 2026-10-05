# Deus

An infrastructure OS for the 2030s, targeting cloud infrastructure, high-performance edge devices, and laptops. It replaces the monolithic Linux paradigm and its costs: ring transitions, CR3 switches, and TLB churn.

The spiritual baseline is TempleOS/ZealOS, for simplicity and a unified JIT/compiler shell. We remove TempleOS's flaws — cooperative multitasking, no security, no networking — and add Language-Based Security (LBS) plus an asynchronous microkernel. Isolation is enforced by the compiler, not the MMU/hardware rings.

You are lead co-developer. Build incrementally. Every milestone must boot in QEMU.

## Hard constraints (do not violate)
- **x86_64 only, for now.** ARM/mobile is out of scope.
- **Paging stays on.** Long mode requires CR0.PG.
  - Build page tables once with 1 GiB pages. Load CR3 once and never reload it. Zero TLB flushes, near-zero misses, no syscall ring transitions.
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

## LBS and the language
- Isolation is enforced by the compiler, not the MMU.
- **Manna** descends from HolyC and has two compile targets:
  - **User apps** compile to WebAssembly and run in a bare-metal Wasm runtime. Budget a 5–20% bounds-check overhead without guard pages.
  - **Kernel code** compiles to native machine code. It must pass a static verifier and carry a `[Kernel_Hook]` attribute to use raw pointers.

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

## Toolchain & run
- Compiler: `clang --target=x86_64-elf` (or `x86_64-elf-gcc`) plus `ld.lld`.
- Bootloader: Limine (UEFI). Use the `limine.h` protocol with a base revision tag.
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
- **Spectre:** a single address space plus LBS leaks across sandboxes. A mitigation plan is required before networking.
- **TCB:** the compiler, verifier, and Wasm runtime all run in ring 0. One bug compromises everything. Fuzz them continuously.
- **DMA bypasses LBS.** An IOMMU (VT-d) is required before untrusted drivers (see DMA protection above).
- **Wasm bounds checks:** without guard pages, expect a 5–20% overhead.
- **Cloud/EPT:** nested paging under a hypervisor adds 2D walks our flat-CR3 design doesn't see on bare metal (see Scope above).

## Later phases
- M2: compiler frontend (ZealC port) with a static pointer-safety verifier and loop counter injection.
- M3: bare-metal Wasm runtime (WAMR) and zero-copy IPC queues.
- M4: telemetry-driven JIT and accelerator interfaces.

## Working conventions
- Small commits, one subsystem each. Run `make run` before claiming success.
- Read `serial.log` and `qemu.log` to verify. Don't assume.
- Ask before changing any hard constraint above.
