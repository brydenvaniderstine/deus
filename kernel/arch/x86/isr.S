# Exception stubs (vectors 0-31) and the spurious-interrupt stub (0xFF).
# The timer stub (0x20) lives in kernel/sched/switch.S as isr_timer.
#
# Every exception stub normalizes the stack to:
#   [vector][error_code][CPU-pushed: RIP][CS][RFLAGS][RSP][SS]
# then jumps to the common handler, which never returns.
    .text
    .global isr_exception_common
    .extern exception_handler

    .altmacro

    .macro ISR_NOERR num
    .global isr\num
isr\num:
    cli
    push $0
    push $\num
    jmp isr_exception_common
    .endm

    .macro ISR_ERR num
    .global isr\num
isr\num:
    cli
    push $\num
    jmp isr_exception_common
    .endm

# Vectors that push a hardware error code.
ISR_ERR 8
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_ERR 17
ISR_ERR 21
ISR_ERR 29
ISR_ERR 30

# All other vectors 0-31 (no hardware error code).
.set i, 0
.rept 32
    .if (i != 8) && (i != 10) && (i != 11) && (i != 12) && (i != 13) && (i != 14) && (i != 17) && (i != 21) && (i != 29) && (i != 30)
    ISR_NOERR %i
    .endif
    .set i, i+1
.endr

isr_exception_common:
    mov  %rsp, %rdi      # arg0 = struct exc_frame*
    call exception_handler
    cli
1:  hlt
    jmp 1b

    .global isr_spurious
isr_spurious:
    iretq
