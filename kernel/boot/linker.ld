OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)

PHDRS
{
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ; /* R+X */
    rodata  PT_LOAD    FLAGS((1 << 2)) ;             /* R   */
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ;  /* R+W */
}

SECTIONS
{
    /* Higher-half kernel link base (CLAUDE.md hard constraint). Limine loads
       the kernel at a physical address of its choosing and reports it via
       the kernel-address request; this is only the *virtual* link address. */
    . = 0xffffffff80000000;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    /* Page-aligned section boundaries, so paging.c can assign per-page
       W^X protection by comparing each 4KiB page's address against
       these instead of guessing where one section ends and the next
       begins. */
    PROVIDE(__rodata_start = .);

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    PROVIDE(__data_start = .);

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /* Marks the first byte past the loaded image, so paging.c can size the
       higher-half kernel-image mapping exactly instead of guessing. */
    PROVIDE(__kernel_end = .);

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
        *(.comment)
    }
}
