/* W^X regression tests (make test-wx only - never linked into the default
 * kernel.elf). Boots the same GDT/IDT/paging bring-up as entry.c, then
 * deliberately violates one page protection, selected at compile time by
 * -DWX_TEST=1/2/3. The existing exception_handler (idt.c) does the actual
 * fault reporting - vector/error_code/RIP/CR2 - to serial. Each subtest is
 * expected to fault and halt there; there is no continuation path. */
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#include "../arch/x86/serial.h"
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/paging.h"
#include "../lib/printf.h"

#ifndef WX_TEST
#error "WX_TEST must be defined to 1, 2, or 3"
#endif

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

static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

#if WX_TEST == 2
/* Non-zero initializer forces this into .data (RW+NX), not .bss. 0xC3 is
 * `ret` - harmless if ever actually reached, which it shouldn't be. */
static volatile uint8_t data_target[16] = { 0xC3 };
#endif

void kmain(void) {
    serial_init();
    kprintf("\n[test-wx] WX_TEST=%d\n", WX_TEST);

    if (!LIMINE_BASE_REVISION_SUPPORTED || !memmap_request.response ||
        !hhdm_request.response || !kaddr_request.response) {
        kprintf("[test-wx] FATAL: missing Limine response\n");
        halt_forever();
    }

    struct limine_memmap_response *memmap = (struct limine_memmap_response *)memmap_request.response;
    struct limine_hhdm_response *hhdm = (struct limine_hhdm_response *)hhdm_request.response;
    struct limine_kernel_address_response *kaddr =
        (struct limine_kernel_address_response *)kaddr_request.response;

    gdt_init();
    idt_init();
    paging_init(memmap, hhdm, kaddr);

    kprintf("[test-wx] protections live; attempting the deliberate violation\n");

#if WX_TEST == 1
    /* Write to a .text address (the kernel's own first byte). Expect #PF,
     * error_code=0x3 (P=1,W=1,U=0), CR2 = the target address. */
    volatile uint8_t *target = (volatile uint8_t *)kaddr->virtual_base;
    kprintf("[test-wx] writing to .text at 0x%lx\n", (uint64_t)target);
    *target = 0x90;
#elif WX_TEST == 2
    /* Execute from a .data address. Expect #PF, error_code=0x11
     * (P=1,W=0,U=0,I/D=1). */
    kprintf("[test-wx] executing .data at 0x%lx\n", (uint64_t)data_target);
    void (*fn)(void) = (void (*)(void))data_target;
    fn();
#elif WX_TEST == 3
    /* Execute via the identity alias of a .text address (the kernel's own
     * first byte, reached through its physical address instead of the
     * higher-half virtual one). Expect #PF, error_code=0x11 - this is the
     * NX-on-identity-map fix actually being exercised. */
    uint64_t identity_text = kaddr->physical_base;
    kprintf("[test-wx] executing .text via identity alias at 0x%lx\n", identity_text);
    void (*fn)(void) = (void (*)(void))identity_text;
    fn();
#endif

    kprintf("[test-wx] FAIL: no fault occurred, protection did not trigger\n");
    halt_forever();
}
