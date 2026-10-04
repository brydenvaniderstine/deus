#include <stdint.h>
#include "clock.h"
#include "pit.h"
#include "../../lib/printf.h"

/* CPUID 0x15 (core crystal clock) + 0x16 (processor frequency) would give
 * an exact bus frequency with no timing loop at all. qemu64 reports leaf
 * 0x15 as unenumerated (eax=ebx=0), and even when it is enumerated on real
 * hardware, converting "crystal Hz" to "LAPIC ticks per ms at /16" still
 * needs the APIC-to-crystal ratio, which isn't derived yet. Stub: detect
 * and log, don't use it for M1. */
static int cpuid15_calibrate(uint32_t *out_ticks_per_ms) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x15), "c"(0));
    if (eax == 0 || ebx == 0) {
        kprintf("[clock] cpuid 0x15: not enumerated (eax=%u ebx=%u ecx=%u), skipping\n", eax, ebx, ecx);
        return 0;
    }
    kprintf("[clock] cpuid 0x15: enumerated (eax=%u ebx=%u ecx=%u) but crystal->LAPIC-tick conversion not implemented, skipping\n",
            eax, ebx, ecx);
    (void)out_ticks_per_ms;
    return 0;
}

/* HPET is a fixed-frequency free-running counter we could poll the same
 * way the PIT gate is polled below, but finding its MMIO base needs an
 * ACPI table walk (RSDP -> XSDT/RSDT -> HPET table) that doesn't exist
 * yet. Stub: log and skip. */
static int hpet_calibrate(uint32_t *out_ticks_per_ms) {
    kprintf("[clock] hpet: not implemented (no ACPI table walk yet), skipping\n");
    (void)out_ticks_per_ms;
    return 0;
}

uint32_t clock_calibrate_lapic_ticks_per_ms(void) {
    uint32_t ticks;

    if (cpuid15_calibrate(&ticks)) {
        kprintf("[clock] calibration source: cpuid15\n");
        return ticks;
    }
    if (hpet_calibrate(&ticks)) {
        kprintf("[clock] calibration source: hpet\n");
        return ticks;
    }
    ticks = pit_calibrate_lapic_ticks_per_ms();
    if (ticks != 0) {
        kprintf("[clock] calibration source: pit\n");
        return ticks;
    }

    kprintf("[clock] FATAL: every calibration source failed\n");
    return 0;
}
