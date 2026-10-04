#pragma once
#include <stdint.h>

#define IDT_ENTRIES   256
#define VEC_TIMER     0x20
#define VEC_SPURIOUS  0xFF

/* Gate type_attr bytes (long mode): present | DPL0 | 64-bit interrupt gate. */
#define GATE_INTR64   0x8E

void idt_init(void);
void idt_set_gate(uint8_t vector, void (*handler)(void), uint16_t selector,
                   uint8_t type_attr, uint8_t ist);

/* Defined in switch.S (kernel/sched). */
extern void isr_timer(void);
