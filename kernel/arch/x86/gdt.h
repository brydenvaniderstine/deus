#pragma once
#include <stdint.h>

#define KCODE_SEL 0x08
#define KDATA_SEL 0x10
#define TSS_SEL   0x18

/* IST slot used for the #DF (double fault) stack. */
#define IST_DF 1

void gdt_init(void);
