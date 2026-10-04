#pragma once
#include <stdint.h>
#include <limine.h>

/* Builds one CR3 containing:
 *   - an identity map of all RAM (per the memmap) plus the 3-4 GiB MMIO
 *     hole, using 1 GiB pages (PDPTE.PS=1), PML4 slot 0.
 *   - a higher-half alias of the kernel's physical load region at
 *     0xffffffff80000000, PML4 slot 511.
 *   - the HHDM range Limine reported, aliased to the same physical RAM,
 *     so Limine response pointers and the boot stack (both HHDM- or
 *     identity-addressed, per Limine's handoff) stay valid after the
 *     CR3 load.
 * Loads CR3 exactly once. Never hardcodes physical placement - everything
 * is derived from the memmap, hhdm, and kernel-address responses. */
void paging_init(struct limine_memmap_response *memmap,
                  struct limine_hhdm_response *hhdm,
                  struct limine_kernel_address_response *kaddr);
