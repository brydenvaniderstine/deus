#pragma once
#include <stdint.h>

/* Reads IA32_APIC_BASE (MSR 0x1B) and logs whether the LAPIC MMIO base
 * matches the 0xFEE00000 default assumed by sched.c. Does not relocate it. */
void lapic_confirm_base(void);
