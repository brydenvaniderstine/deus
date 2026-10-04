#pragma once
#include <stdint.h>

/* Calibration cascade: CPUID 0x15/0x16 -> HPET -> PIT (CLAUDE.md). Logs
 * which source won. Returns LAPIC ticks-per-ms at the current divide
 * setting (lapic_timer_init must have run first to enable/divide the
 * LAPIC timer), or 0 if every source failed. */
uint32_t clock_calibrate_lapic_ticks_per_ms(void);
