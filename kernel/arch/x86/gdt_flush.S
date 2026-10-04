# Reload GDTR, then the data segments and CS (via a far return trampoline),
# then load the task register.
    .text
    .global gdt_flush
    .global tss_flush

# rdi = pointer to gdtr {limit:16, base:64}
gdt_flush:
    lgdt (%rdi)

    mov  $0x10, %ax     # KDATA_SEL
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs
    mov  %ax, %ss

    # Reload CS via a far return: push target CS, push target RIP, retfq.
    # lretq pops exactly those two back off, leaving the stack exactly as
    # it was on entry (this function's own return address still on top).
    push $0x08            # KCODE_SEL
    lea  1f(%rip), %rax
    push %rax
    lretq
1:
    ret

# di = TSS selector
tss_flush:
    mov  %di, %ax
    ltr  %ax
    ret
