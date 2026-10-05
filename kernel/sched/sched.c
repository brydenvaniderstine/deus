// Deus Phase 2: preemptive round-robin via LAPIC periodic timer
#include <stdint.h>
#include <stddef.h>

#define LAPIC_BASE      0xFEE00000ULL   // confirm via IA32_APIC_BASE MSR (0x1B)
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_LVT_TMR   0x320
#define LAPIC_TMR_INIT  0x380
#define LAPIC_TMR_DIV   0x3E0
#define TIMER_VEC       0x20
#define SPURIOUS_VEC    0xFF

#define KCODE_SEL 0x08
#define KDATA_SEL 0x10

// Telemetry ring: 0x04000000-0x07FFFFFF per memory map
#define TELEM_BASE  0x04000000ULL
#define TELEM_SLOTS ((0x08000000ULL - TELEM_BASE) / 8)   // 0x800000, pow2

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;               // CPU-pushed
} frame_t;

typedef struct task {
    uint64_t     rsp;      // saved frame_t*
    uint64_t     ticks;
    struct task *next;     // circular run queue
} task_t;

extern void isr_timer(void);

static task_t  *cur;
static uint64_t telem_head;

static inline void lapic_w(uint32_t r, uint32_t v) {
    *(volatile uint32_t *)(LAPIC_BASE + r) = v;
}

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(p));
}

static inline void telemetry_record(uint64_t rip) {
    ((volatile uint64_t *)TELEM_BASE)[telem_head++ & (TELEM_SLOTS - 1)] = rip;
}

// Called from isr_timer. Returns next frame pointer.
uint64_t sched_tick(uint64_t rsp) {
    cur->rsp = rsp;
    cur->ticks++;
    telemetry_record(((frame_t *)rsp)->rip);
    cur = cur->next;                 // TODO: MicroSchedulerSelectNext()
    lapic_w(LAPIC_EOI, 0);
    return cur->rsp;
}

// Build a fake interrupt frame so first iretq jumps to entry.
void task_init(task_t *t, void (*entry)(void), uint8_t *stack, size_t sz) {
    uint64_t top = ((uint64_t)(stack + sz)) & ~0xFULL;
    frame_t *f = (frame_t *)top - 1;
    for (size_t i = 0; i < sizeof *f / 8; i++) ((uint64_t *)f)[i] = 0;
    f->rip    = (uint64_t)entry;
    f->cs     = KCODE_SEL;
    f->rflags = 0x202;               // IF=1
    f->rsp    = top - 8;             // ABI: rsp%16==8 at fn entry
    f->ss     = KDATA_SEL;
    t->rsp    = (uint64_t)f;
    t->ticks  = 0;
}

void sched_start(task_t *first) { cur = first; }

void lapic_timer_init(uint32_t count) {
    outb(0x21, 0xFF);                         // mask legacy PIC
    outb(0xA1, 0xFF);
    lapic_w(LAPIC_SVR, 0x100 | SPURIOUS_VEC); // enable LAPIC
    lapic_w(LAPIC_TMR_DIV, 0x3);              // /16
    lapic_w(LAPIC_LVT_TMR, TIMER_VEC | (1u << 17)); // periodic
    lapic_w(LAPIC_TMR_INIT, count);           // TODO: calibrate vs PIT/HPET
}

// Install: idt_set_gate(TIMER_VEC, isr_timer, KCODE_SEL, 0x8E); then sti.
