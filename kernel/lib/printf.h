#pragma once

/* Minimal freestanding formatter: %s %c %d %u %x %p (lowercase hex) and %% only. */
void kprintf(const char *fmt, ...);
