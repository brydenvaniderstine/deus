#include <stdint.h>
#include "pit.h"
#include "../../lib/printf.h"

#define PIT_CH2_DATA   0x42
#define PIT_CMD        0x43
#define PIT_GATE_PORT  0x61
#define PIT_HZ         1193182ULL
#define CAL_MS         10
/* mode 0 (interrupt on terminal count), channel 2, lobyte/hibyte access */
#define PIT_CH2_MODE0  0xB0

#define LAPIC_BASE     0xFEE00000ULL
#define LAPIC_TMR_INIT 0x380
#define LAPIC_TMR_CURR 0x390

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(p));
}

static inline uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

static inline uint32_t lapic_r(uint32_t reg) {
    return *(volatile uint32_t *)(LAPIC_BASE + reg);
}

static inline void lapic_w(uint32_t reg, uint32_t v) {
    *(volatile uint32_t *)(LAPIC_BASE + reg) = v;
}

uint32_t pit_calibrate_lapic_ticks_per_ms(void) {
    uint16_t count = (uint16_t)((PIT_HZ * CAL_MS) / 1000);

    /* Gate low, program channel 2, load count, then gate high to start. */
    uint8_t gate = inb(PIT_GATE_PORT);
    outb(PIT_GATE_PORT, gate & ~0x03); /* gate off, speaker off */
    outb(PIT_CMD, PIT_CH2_MODE0);
    outb(PIT_CH2_DATA, count & 0xFF);
    outb(PIT_CH2_DATA, (count >> 8) & 0xFF);

    lapic_w(LAPIC_TMR_INIT, 0xFFFFFFFF); /* one-shot, max count, masked state irrelevant while polling */

    gate = inb(PIT_GATE_PORT);
    outb(PIT_GATE_PORT, (gate & ~0x02) | 0x01); /* gate on, speaker off: start counting */

    uint32_t timeout = 10000000;
    while (!(inb(PIT_GATE_PORT) & 0x20)) {
        if (--timeout == 0) {
            kprintf("[pit] calibration timed out waiting for channel 2 output\n");
            return 0;
        }
    }
    uint32_t remaining = lapic_r(LAPIC_TMR_CURR);
    uint32_t elapsed = 0xFFFFFFFF - remaining;
    uint32_t per_ms = elapsed / CAL_MS;

    kprintf("[pit] calibrated over %dms: lapic ticks elapsed=%u -> %u ticks/ms (div16)\n",
            CAL_MS, elapsed, per_ms);
    return per_ms;
}
