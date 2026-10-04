#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#include "../arch/x86/serial.h"
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/paging.h"
#include "../arch/x86/lapic.h"
#include "../arch/x86/pit.h"
#include "../sched/sched.h"
#include "../lib/printf.h"

__attribute__((used))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
};

__attribute__((used))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
};

__attribute__((used))
static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
};

static const char *memmap_type_name(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:                 return "usable";
        case LIMINE_MEMMAP_RESERVED:               return "reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:        return "acpi_reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS:                return "acpi_nvs";
        case LIMINE_MEMMAP_BAD_MEMORY:              return "bad_memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:  return "bootloader_reclaimable";
        case LIMINE_MEMMAP_KERNEL_AND_MODULES:      return "kernel_and_modules";
        case LIMINE_MEMMAP_FRAMEBUFFER:             return "framebuffer";
        default:                                    return "unknown";
    }
}

static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

static task_t taskA; /* becomes "task A" in place: kmain falls into printA() below */
static task_t taskB;
static uint8_t taskB_stack[4096];

static void printA(void) {
    for (;;) {
        serial_putc('A');
        for (volatile long i = 0; i < 2000000; i++);
    }
}

static void printB(void) {
    for (;;) {
        serial_putc('B');
        for (volatile long i = 0; i < 2000000; i++);
    }
}

void kmain(void) {
    serial_init();
    kprintf("\n[boot] LSOS Milestone 1\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        kprintf("[boot] FATAL: Limine base revision not supported\n");
        halt_forever();
    }

    if (!memmap_request.response || !hhdm_request.response || !kaddr_request.response) {
        kprintf("[boot] FATAL: missing Limine response (memmap=%p hhdm=%p kaddr=%p)\n",
                (void *)memmap_request.response, (void *)hhdm_request.response,
                (void *)kaddr_request.response);
        halt_forever();
    }

    struct limine_memmap_response *memmap = (struct limine_memmap_response *)memmap_request.response;
    struct limine_hhdm_response *hhdm = (struct limine_hhdm_response *)hhdm_request.response;
    struct limine_kernel_address_response *kaddr =
        (struct limine_kernel_address_response *)kaddr_request.response;

    kprintf("[boot] memmap: %lu entries\n", memmap->entry_count);
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        kprintf("  [%2lu] base=0x%lx length=0x%lx type=%s\n",
                i, e->base, e->length, memmap_type_name(e->type));
    }
    kprintf("[boot] hhdm offset=0x%lx\n", hhdm->offset);
    kprintf("[boot] kernel physical_base=0x%lx virtual_base=0x%lx\n",
            kaddr->physical_base, kaddr->virtual_base);

    gdt_init();
    kprintf("[gdt] loaded: null/kcode(0x08)/kdata(0x10)/tss(0x18), IST1=#DF stack\n");

    idt_init();

    paging_init(memmap, hhdm, kaddr);

    lapic_confirm_base();
    lapic_timer_init(0xFFFFFFFF); /* enable LAPIC, div16, periodic LVT - count is a placeholder */
    uint32_t ticks_per_ms = pit_calibrate_lapic_ticks_per_ms();
    if (ticks_per_ms == 0) {
        kprintf("[boot] FATAL: PIT calibration failed, no CPUID 0x15/HPET fallback implemented yet\n");
        halt_forever();
    }
    lapic_timer_init(ticks_per_ms);
    kprintf("[boot] LAPIC periodic timer armed at %u ticks/ms\n", ticks_per_ms);

    task_init(&taskB, printB, taskB_stack, sizeof(taskB_stack));
    taskA.next = &taskB;
    taskB.next = &taskA;
    sched_start(&taskA);

    kprintf("[boot] enabling interrupts, starting A/B preemption proof\n");
    __asm__ volatile ("sti");

    printA(); /* never returns; this stack/context is "task A" from here on */
}
