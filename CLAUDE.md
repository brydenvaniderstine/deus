# LSOS — Liquid Silicon Operating System

Bare-metal x86_64 hybrid microkernel. The spiritual baseline is TempleOS/ZealOS: simplicity and a unified JIT shell. Isolation comes from Language-Based Security (LBS) rather than hardware rings.
You are lead co-developer. Build incrementally. Every milestone must boot in QEMU.

## Hard constraints (do not violate)
- **x86_64 only, for now.** ARM/mobile is out of scope.
- **Paging stays on.** Long mode requires CR0.PG.
  - Identity-map all RAM once with 1 GiB pages, and also map the MMIO holes.
  - Load CR3 once and never reload it. Zero TLB flushes.
- **Kernel links higher-half at 0xffffffff80000000.** Limine requires this.
  - Our CR3 must contain BOTH the identity map AND the higher-half alias of the kernel's physical load region.
- **Never hardcode physical placement.** Read the Limine memmap response, then place the regions below around reserved/firmware areas.
- **MMIO caching.** The LAPIC is at 0xFEE00000 (confirm via MSR 0x1B), and PCI BARs may sit above 4 GiB.
  - Rely on firmware MTRRs for UC, or set PCD/PWT on MMIO 1 GiB pages. Document the choice.
- **Preemption** comes from the LAPIC periodic timer. Cooperative-only scheduling is banned.
- **Compiler flags:** freestanding, `-mno-red-zone`, `-mcmodel=kernel`, `-mgeneral-regs-only`.
  - The last one avoids saving FPU/SSE state in ISRs until we add XSAVE.

## Target memory regions (logical; physical placement per memmap)
| Range | Purpose |
|---|---|
| 0x0 – 0x100000 | Reserved (firmware, Limine handoff) |
| 0x100000 – 0x3FFFFFF | Microkernel text/data |
| 0x4000000 – 0x7FFFFFF | Telemetry rings, JIT profiling tables |
| 0x8000000 – end of RAM | Shared heap, linear Wasm sandboxes |

If the memmap conflicts with these ranges, log it over serial. Then relocate the region. Do not overwrite firmware memory.

## Toolchain & run
- Compiler: `clang --target=x86_64-elf` (or `x86_64-elf-gcc`) plus `ld.lld`.
- Bootloader: Limine (UEFI). Use the `limine.h` protocol with a base revision tag.
- Disk: a GPT image with a FAT32 ESP.
  - ESP holds `EFI/BOOT/BOOTX64.EFI`, `limine.conf`, and the kernel ELF.
- Run:
  ```
  qemu-system-x86_64 -m 8G -cpu qemu64,+apic,+pdpe1gb \
    -bios /path/to/OVMF.fd -drive format=raw,file=lsos_disk.img \
    -serial file:serial.log -monitor stdio -no-reboot -d int,cpu_reset -D qemu.log
  ```
- `+pdpe1gb` is mandatory, because qemu64 lacks 1 GiB page support by default.
- Provide a `Makefile` with these targets: `all`, `image`, `run`, `debug` (adds `-s -S`), and `clean`.

## Repo layout (proposed)
```
kernel/
  boot/      entry.c, limine requests, linker.ld
  arch/x86/  gdt.c, idt.c, isr.S, paging.c, lapic.c, pit.c, serial.c
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
- The LAPIC timer count is currently a hardcoded TODO. Milestone 1 replaces it with PIT calibration.

## Milestone 1 — boot foundation (DO THIS FIRST)
1. **Limine stub:** Implement the base revision plus memmap, HHDM, and kernel-address requests. Add a serial (COM1) logger. Halt cleanly on a missing response.
2. **GDT:** null, kernel code 64, kernel data. Add a TSS with an IST stack for #DF. Reload the segments after `lgdt`.
3. **IDT:** 256 entries.
   - Exceptions 0–31 dump the vector, error code, RIP, and CR2 to serial, then halt.
   - Vector 0x20 is the timer; 0xFF is spurious.
4. **Paging:**
   - Detect max phys RAM from the memmap. Build the PML4/PDPT with 1 GiB pages covering RAM plus the 3–4 GiB MMIO hole.
   - Add the higher-half alias for the kernel. Load CR3 once.
   - Verify: kernel survives the switch, and an identity access to a high RAM address works.
5. **PIT calibration:** Program PIT ch2 for a ~10 ms one-shot. Count LAPIC ticks (div 16) over that window. Derive the count per 1 ms tick.
   - Fallbacks, stubbed and logged: CPUID 0x15, HPET.
6. **Preemption proof:** Two kernel tasks print 'A'/'B' to serial in loops with no yields. The interleaving output proves preemption.

**Acceptance:** `make run` boots. Serial log shows the memmap, the CR3 load, the calibrated tick rate, and interleaved A/B output. No triple faults.

## Open risks (track; don't solve in M1)
- **DMA bypasses LBS.** An IOMMU (VT-d) is required before untrusted drivers.
- **Spectre:** a single address space plus LBS leaks across sandboxes. A mitigation plan is needed before networking.
- **TCB size:** the JIT and Wasm runtime run in ring 0. Plan fuzzing and a minimal verifier.
- **Wasm bounds checks:** without guard pages, expect a 5–20% overhead.
- **ASIC/accelerator routing** is research only. No architecture commitments yet.

## Later phases
- M2: compiler frontend (ZealC port) with a static pointer-safety verifier and loop counter injection.
- M3: bare-metal Wasm runtime (WAMR) and zero-copy IPC queues.
- M4: telemetry-driven JIT and accelerator interfaces.

## Working conventions
- Small commits, one subsystem each. Run `make run` before claiming success.
- Read `serial.log` and `qemu.log` to verify. Don't assume.
- Ask before changing any hard constraint above.
