#include <stdint.h>
#include "gdt.h"
#include "../../lib/string.h"

/* Plain 64-bit code/data descriptors (base/limit ignored in long mode) plus
 * one 16-byte TSS descriptor. Layout fixes KCODE_SEL=0x08, KDATA_SEL=0x10,
 * TSS_SEL=0x18 to match sched.c's hardcoded selectors. */

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct tss_entry_low {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct tss_entry_high {
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

#define GDT_ENTRIES 5 /* null, kcode, kdata, tss_low, tss_high */

static struct gdt_entry  gdt[GDT_ENTRIES];
static struct gdtr       gdtr;
static struct tss64      tss __attribute__((aligned(16)));
static uint8_t           df_stack[4096] __attribute__((aligned(16)));

extern void gdt_flush(uint64_t gdtr_addr);
extern void tss_flush(uint16_t sel);

static void set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_high   = (base >> 24) & 0xFF;
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].granularity = (limit >> 16) & 0x0F;
    gdt[i].granularity |= gran & 0xF0;
    gdt[i].access      = access;
}

void gdt_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.ist1 = (uint64_t)(df_stack + sizeof(df_stack));
    tss.iomap_base = sizeof(tss);

    set_entry(0, 0, 0, 0, 0);                         /* null */
    set_entry(1, 0, 0, 0x9A, 0x20);                   /* kcode: present, ring0, exec/read, long-mode (L bit) */
    set_entry(2, 0, 0, 0x92, 0x00);                   /* kdata: present, ring0, read/write */

    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    struct tss_entry_low *tl = (struct tss_entry_low *)&gdt[3];
    tl->limit_low   = tss_limit & 0xFFFF;
    tl->base_low    = tss_base & 0xFFFF;
    tl->base_mid    = (tss_base >> 16) & 0xFF;
    tl->access      = 0x89; /* present, ring0, 64-bit TSS (available) */
    tl->granularity = (tss_limit >> 16) & 0x0F;
    tl->base_high   = (tss_base >> 24) & 0xFF;

    struct tss_entry_high *th = (struct tss_entry_high *)&gdt[4];
    th->base_upper = (tss_base >> 32) & 0xFFFFFFFF;
    th->reserved   = 0;

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)&gdt;

    gdt_flush((uint64_t)&gdtr);
    tss_flush(TSS_SEL);
}
