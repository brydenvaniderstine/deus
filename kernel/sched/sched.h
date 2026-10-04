#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct task {
    uint64_t     rsp;
    uint64_t     ticks;
    struct task *next;
} task_t;

void task_init(task_t *t, void (*entry)(void), uint8_t *stack, size_t sz);
void sched_start(task_t *first);
void lapic_timer_init(uint32_t count);
