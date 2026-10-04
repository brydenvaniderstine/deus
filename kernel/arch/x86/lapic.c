#include <stdint.h>
#include "lapic.h"
#include "../../lib/printf.h"

#define IA32_APIC_BASE 0x1B
#define EXPECTED_LAPIC_BASE 0xFEE00000ULL

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void lapic_confirm_base(void) {
    uint64_t base = rdmsr(IA32_APIC_BASE) & ~0xFFFULL;
    if (base == EXPECTED_LAPIC_BASE) {
        kprintf("[lapic] base confirmed at 0x%lx via MSR 0x1B\n", base);
    } else {
        kprintf("[lapic] WARNING: base is 0x%lx, expected 0x%lx - sched.c's LAPIC_BASE is wrong for this machine\n",
                base, EXPECTED_LAPIC_BASE);
    }
}
