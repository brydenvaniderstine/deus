#include <stdint.h>
#include <stddef.h>
#include "paging.h"
#include "../../lib/printf.h"
#include "../../lib/string.h"

#define PAGE_4KIB      0x1000ULL
#define PAGE_1GIB      0x40000000ULL
#define PML4_SIZE      0x8000000000ULL /* 512 GiB: one PML4 entry's span */
#define PAGE_PRESENT   0x01ULL
#define PAGE_WRITE     0x02ULL
#define PAGE_NX        (1ULL << 63) /* execute-disable; needs EFER.NXE=1 */
#define PRESENT_RW     0x03ULL   /* present | writable - for page-table POINTERS
                                  * (PML4E/PDPTE/PDE), never a W^X leaf PTE */
#define PRESENT_RW_PS  0x83ULL   /* present | writable | PS (1 GiB page) */
#define PCD_BIT        0x10ULL   /* page cache disable, set on the MMIO hole */

/* Defined by linker.ld: page-aligned section boundaries, so each kernel-image
 * leaf PTE can get the right W^X flags instead of one blanket present+writable
 * for the whole image. __kernel_end is the first byte past .bss. */
extern char __rodata_start[];
extern char __data_start[];
extern char __kernel_end[];

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

#define IA32_EFER  0xC0000080ULL
#define EFER_NXE   (1ULL << 11)

/* Must run before the CR3 load that installs NX-bit leaf PTEs: NX is only
 * honored when EFER.NXE=1, and a page's NX bit is otherwise just ignored
 * (silently executable), which would make the W^X split above a no-op. */
static inline void enable_nxe(void) {
    wrmsr(IA32_EFER, rdmsr(IA32_EFER) | EFER_NXE);
}

/* Without CR0.WP, ring-0 (supervisor) writes ignore the page tables' R/W
 * bit entirely and succeed even against a read-only page - the .rodata/
 * .text protection above would be unenforced for our own kernel code. */
static inline void enable_cr0_wp(void) {
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16);
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
}

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
static uint64_t pdpt_hhdm[512]  __attribute__((aligned(4096)));
/* The higher-half kernel-image mapping (see paging_init) uses 4 KiB pages,
 * not 1 GiB: virtual_base is architecturally fixed at a 1 GiB-aligned
 * address (offset 0 within its frame), but Limine's physical_base is only
 * ever guaranteed page-aligned - a 1 GiB page can only alias the two when
 * their low 30 bits happen to match, which they generally won't. 4 KiB
 * pages have no such constraint. One PT covers 2 MiB, comfortably more
 * than Milestone 1's kernel image needs. */
static uint64_t pd_kernel[512]  __attribute__((aligned(4096)));
static uint64_t pt_kernel[512]  __attribute__((aligned(4096)));

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

    /* RESERVED/BAD_MEMORY entries aren't RAM and nothing here dereferences
     * them as such - a sparse one (e.g. QEMU q35's high 64-bit PCI MMIO
     * window) can sit far above any real RAM and isn't worth paying for
     * in a single 512-entry PDPT. The known 3-4 GiB MMIO hole is still
     * unconditionally covered by the floor below regardless. Mapping a
     * high PCI64 BAR window for real would need a second PML4/PDPT entry -
     * tracked as a gap, not needed by any Milestone 1 code path. */
    uint64_t max_phys = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_RESERVED || e->type == LIMINE_MEMMAP_BAD_MEMORY) continue;
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

    /* NX on every identity-map entry (and, by extension, the HHDM alias:
     * same flags, whether reused directly or copied - see below): the
     * kernel only ever executes via the higher-half alias, which has its
     * own per-page W^X split (see above). Nothing should be executable
     * through these low/HHDM addresses at all. */
    kprintf("[paging] identity-mapping 0..0x%lx with %lu x 1GiB pages (NX)\n", max_phys, pages_needed);
    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t flags = PRESENT_RW_PS | PAGE_NX;
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
     * index 510 covers exactly 0xffffffff80000000-0xffffffffbfffffff. That
     * 1 GiB window is further split into 2 MiB (PD) and 4 KiB (PT) steps
     * for just this mapping - see the pd_kernel/pt_kernel comment above. */
    memset(pd_kernel, 0, sizeof(pd_kernel));
    memset(pt_kernel, 0, sizeof(pt_kernel));

    uint64_t kernel_size = (uint64_t)__kernel_end - kaddr->virtual_base;
    uint64_t kernel_pages = (kernel_size + PAGE_4KIB - 1) / PAGE_4KIB;
    if (kernel_pages > 512) {
        kprintf("[paging] FATAL: kernel image is %lu bytes (%lu pages), exceeds one 2MiB PT\n",
                kernel_size, kernel_pages);
        for (;;) __asm__ volatile ("cli; hlt");
    }
    kprintf("[paging] higher-half alias: 0xffffffff80000000 -> phys 0x%lx, %lu bytes (%lu x 4KiB pages)\n",
            kaddr->physical_base, kernel_size, kernel_pages);
    /* Per-page W^X: .text is R+X (no write, no NX), .rodata is R+NX (no
     * write), .data/.bss is RW+NX. Boundaries come from linker.ld, which
     * page-aligns each section, so address comparison alone is exact -
     * no need to know which section a given page "mostly" belongs to. */
    uint64_t rodata_start = (uint64_t)__rodata_start;
    uint64_t data_start    = (uint64_t)__data_start;
    for (uint64_t i = 0; i < kernel_pages; i++) {
        uint64_t virt = kaddr->virtual_base + i * PAGE_4KIB;
        uint64_t flags;
        if (virt < rodata_start)      flags = PAGE_PRESENT;                       /* .text: R+X */
        else if (virt < data_start)   flags = PAGE_PRESENT | PAGE_NX;             /* .rodata: R+NX */
        else                          flags = PAGE_PRESENT | PAGE_WRITE | PAGE_NX; /* .data/.bss: RW+NX */
        pt_kernel[i] = (kaddr->physical_base + i * PAGE_4KIB) | flags;
    }
    pd_kernel[0] = virt_to_phys(kaddr, (uint64_t)pt_kernel) | PRESENT_RW;
    pdpt_high[510] = virt_to_phys(kaddr, (uint64_t)pd_kernel) | PRESENT_RW;
    pml4[511] = virt_to_phys(kaddr, (uint64_t)pdpt_high) | PRESENT_RW;

    enable_nxe(); /* before the CR3 load below, so NX is honored the instant it's live */
    kprintf("[paging] EFER.NXE enabled\n");

    uint64_t pml4_phys = virt_to_phys(kaddr, (uint64_t)pml4);
    load_cr3(pml4_phys);
    kprintf("[paging] CR3 loaded at phys 0x%lx (virt 0x%lx); kernel survived the switch\n",
            pml4_phys, (uint64_t)pml4);

    enable_cr0_wp();
    kprintf("[paging] CR0.WP enabled\n");

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
