#pragma once
#include <stdint.h>

/* Programs PIT channel 2 for a ~10ms one-shot, polls it to completion while
 * sampling the LAPIC timer's current-count register, and derives the LAPIC
 * tick count for a 1ms period at the current divide setting (/16, set by
 * lapic_timer_init before calling this). Returns 0 on failure (logged). */
uint32_t pit_calibrate_lapic_ticks_per_ms(void);
