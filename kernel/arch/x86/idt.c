#include <stdint.h>
#include "idt.h"
#include "gdt.h"
#include "../../lib/string.h"
#include "../../lib/printf.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* Stack layout isr_exception_common hands to exception_handler. */
struct exc_frame {
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

static struct idt_entry idt[IDT_ENTRIES];
static struct idtr       idtr;

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);
extern void isr_spurious(void);

static void (*const exception_stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

void idt_set_gate(uint8_t vector, void (*handler)(void), uint16_t selector,
                   uint8_t type_attr, uint8_t ist) {
    uint64_t addr = (uint64_t)handler;
    idt[vector].offset_low  = addr & 0xFFFF;
    idt[vector].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].selector    = selector;
    idt[vector].ist         = ist & 0x7;
    idt[vector].type_attr   = type_attr;
    idt[vector].reserved    = 0;
}

void exception_handler(struct exc_frame *f) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    kprintf("\n[exception] vector=%lu error_code=0x%lx rip=0x%lx cs=0x%lx rflags=0x%lx rsp=0x%lx ss=0x%lx cr2=0x%lx\n",
            f->vector, f->error_code, f->rip, f->cs, f->rflags, f->rsp, f->ss, cr2);
    kprintf("[exception] halting\n");
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));

    for (int i = 0; i < 32; i++) {
        uint8_t ist = (i == 8) ? IST_DF : 0; /* #DF runs on its own IST stack */
        idt_set_gate((uint8_t)i, exception_stubs[i], KCODE_SEL, GATE_INTR64, ist);
    }
    idt_set_gate(VEC_TIMER, isr_timer, KCODE_SEL, GATE_INTR64, 0);
    idt_set_gate(VEC_SPURIOUS, isr_spurious, KCODE_SEL, GATE_INTR64, 0);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;
    __asm__ volatile ("lidt (%0)" :: "r"(&idtr));

    kprintf("[idt] 256 entries loaded; vector 0x20=timer 0xff=spurious\n");
}
