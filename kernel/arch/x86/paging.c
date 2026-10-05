#include <stdint.h>
#include <stddef.h>
#include "paging.h"
#include "../../lib/printf.h"
#include "../../lib/string.h"

#define PAGE_1GIB      0x40000000ULL
#define PML4_SIZE      0x8000000000ULL /* 512 GiB: one PML4 entry's span */
#define PRESENT_RW_PS  0x83ULL   /* present | writable | PS (1 GiB page) */
#define PCD_BIT        0x10ULL   /* page cache disable, set on the MMIO hole */

/* 3-4 GiB is documented (CLAUDE.md) as the MMIO hole; we mark it UC via
 * PCD on that single PDPT entry rather than relying on firmware MTRRs, so
 * the choice is explicit and doesn't depend on platform defaults. */
#define MMIO_HOLE_PDPT_INDEX 3

/* Static, page-aligned page-table storage - no dynamic allocator exists
 * yet in Milestone 1, so these live in .bss. */
static uint64_t pml4[512]      __attribute__((aligned(4096)));
static uint64_t pdpt_low[512]  __attribute__((aligned(4096)));
static uint64_t pdpt_high[512] __attribute__((aligned(4096)));
/* Only populated if the HHDM offset isn't 512 GiB-aligned (see below). */
static uint64_t pdpt_hhdm[512] __attribute__((aligned(4096)));

static inline void load_cr3(uint64_t phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(phys) : "memory");
}

/* pml4/pdpt_low/pdpt_high/pdpt_hhdm are static arrays, so (uint64_t)&array
 * is their link-time HIGHER-HALF VIRTUAL address. CR3 and every PML4/PDPT
 * entry's pointer field are consumed directly by the CPU's page-walk
 * hardware, which does zero translation - they must hold physical
 * addresses, or the very first page-walk after load_cr3 triple-faults.
 * Limine's kernel-address response gives the one fact needed to convert:
 * this image's actual physical load address vs. its link-time virtual
 * address, both for the SAME byte. */
static inline uint64_t virt_to_phys(struct limine_kernel_address_response *kaddr, uint64_t virt) {
    return kaddr->physical_base + (virt - kaddr->virtual_base);
}

void paging_init(struct limine_memmap_response *memmap,
                  struct limine_hhdm_response *hhdm,
                  struct limine_kernel_address_response *kaddr) {
    memset(pml4, 0, sizeof(pml4));
    memset(pdpt_low, 0, sizeof(pdpt_low));
    memset(pdpt_high, 0, sizeof(pdpt_high));
    memset(pdpt_hhdm, 0, sizeof(pdpt_hhdm));

    uint64_t max_phys = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        uint64_t end = e->base + e->length;
        if (end > max_phys) max_phys = end;
    }
    if (max_phys < 4ULL * PAGE_1GIB) max_phys = 4ULL * PAGE_1GIB; /* always cover the MMIO hole */

    uint64_t pages_needed = (max_phys + PAGE_1GIB - 1) / PAGE_1GIB;
    if (pages_needed > 512) {
        kprintf("[paging] max_phys=0x%lx needs %lu GiB pages, only 512 (512 GiB) supported\n",
                max_phys, pages_needed);
        for (;;) __asm__ volatile ("cli; hlt");
    }

    kprintf("[paging] identity-mapping 0..0x%lx with %lu x 1GiB pages\n", max_phys, pages_needed);
    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t flags = PRESENT_RW_PS;
        if (i == MMIO_HOLE_PDPT_INDEX) flags |= PCD_BIT;
        pdpt_low[i] = (i * PAGE_1GIB) | flags;
    }
    pml4[0] = virt_to_phys(kaddr, (uint64_t)pdpt_low) | 0x03; /* present | writable */

    /* HHDM alias: Limine's response pointers (memmap entries, etc.) and the
     * boot stack it handed us may be addressed via hhdm_offset+phys rather
     * than identity, per the Limine protocol. Without this mapping, the
     * first access to one of those after the CR3 load triple-faults. */
    uint64_t hhdm_offset = hhdm->offset;
    uint64_t hhdm_pml4_idx = (hhdm_offset >> 39) & 511;
    if (hhdm_pml4_idx == 0 || hhdm_pml4_idx == 511) {
        kprintf("[paging] FATAL: hhdm pml4[%lu] collides with the identity/higher-half slot\n",
                hhdm_pml4_idx);
        for (;;) __asm__ volatile ("cli; hlt");
    }
    if ((hhdm_offset & (PML4_SIZE - 1)) == 0) {
        pml4[hhdm_pml4_idx] = virt_to_phys(kaddr, (uint64_t)pdpt_low) | 0x03; /* share pdpt_low: already identity */
        kprintf("[paging] hhdm alias: pml4[%lu] -> pdpt_low (offset=0x%lx, 512GiB-aligned)\n",
                hhdm_pml4_idx, hhdm_offset);
    } else {
        /* Not 512 GiB-aligned: build a dedicated PDPT, shifted so entry
         * [sub_gib + i] maps physical i*1GiB, same flags as pdpt_low. */
        uint64_t sub_gib = (hhdm_offset >> 30) & 511;
        if ((hhdm_offset & (PAGE_1GIB - 1)) != 0) {
            kprintf("[paging] FATAL: hhdm offset 0x%lx isn't even 1GiB-aligned, can't alias with 1GiB pages\n",
                    hhdm_offset);
            for (;;) __asm__ volatile ("cli; hlt");
        }
        if (sub_gib + pages_needed > 512) {
            kprintf("[paging] FATAL: hhdm offset 0x%lx + %lu GiB of RAM overflows a single PDPT\n",
                    hhdm_offset, pages_needed);
            for (;;) __asm__ volatile ("cli; hlt");
        }
        for (uint64_t i = 0; i < pages_needed; i++) {
            pdpt_hhdm[sub_gib + i] = pdpt_low[i];
        }
        pml4[hhdm_pml4_idx] = virt_to_phys(kaddr, (uint64_t)pdpt_hhdm) | 0x03;
        kprintf("[paging] hhdm alias: pml4[%lu] -> dedicated pdpt_hhdm (offset=0x%lx, not 512GiB-aligned, sub_gib=%lu)\n",
                hhdm_pml4_idx, hhdm_offset, sub_gib);
    }

    /* Higher-half alias: PML4[511] covers the top 512 GiB; within it, PDPT
     * index 510 covers exactly 0xffffffff80000000-0xffffffffbfffffff. */
    uint64_t kphys_aligned = kaddr->physical_base & ~(PAGE_1GIB - 1);
    kprintf("[paging] higher-half alias: 0xffffffff80000000 -> 0x%lx (kernel phys_base=0x%lx virt_base=0x%lx)\n",
            kphys_aligned, kaddr->physical_base, kaddr->virtual_base);
    pdpt_high[510] = kphys_aligned | PRESENT_RW_PS;
    pml4[511] = virt_to_phys(kaddr, (uint64_t)pdpt_high) | 0x03;

    uint64_t pml4_phys = virt_to_phys(kaddr, (uint64_t)pml4);
    load_cr3(pml4_phys);
    kprintf("[paging] CR3 loaded at phys 0x%lx (virt 0x%lx); kernel survived the switch\n",
            pml4_phys, (uint64_t)pml4);

    /* Verify the identity map reaches high RAM: probe just below max_phys. */
    volatile uint64_t *probe = (volatile uint64_t *)(max_phys - 8);
    uint64_t pattern = 0xA5A5A5A5DEADBEEFULL;
    *probe = pattern;
    if (*probe == pattern) {
        kprintf("[paging] identity access at 0x%lx (near top of RAM) verified OK\n", (uint64_t)probe);
    } else {
        kprintf("[paging] FATAL: identity access at 0x%lx did not read back correctly\n", (uint64_t)probe);
        for (;;) __asm__ volatile ("cli; hlt");
    }
}
