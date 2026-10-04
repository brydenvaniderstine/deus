# LSOS Phase 2: LAPIC timer ISR + context switch (x86_64, GAS)
# Entry: CPU pushed SS,RSP,RFLAGS,CS,RIP (40B). +120B GPRs => RSP 16B-aligned at call.
    .text
    .global isr_timer
    .extern sched_tick

isr_timer:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    mov  %rsp, %rdi          # arg0 = current frame
    cld
    call sched_tick          # rax = next task's frame
    mov  %rax, %rsp

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    iretq
