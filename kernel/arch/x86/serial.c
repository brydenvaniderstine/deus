#include <stdint.h>
#include "serial.h"

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);    /* disable interrupts */
    outb(COM1 + 3, 0x80);    /* enable DLAB */
    outb(COM1 + 0, 0x01);    /* divisor low: 115200 baud */
    outb(COM1 + 1, 0x00);    /* divisor high */
    outb(COM1 + 3, 0x03);    /* 8N1, DLAB off */
    outb(COM1 + 2, 0xC7);    /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);    /* IRQs disabled, RTS/DSR set */
}

static int tx_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    while (!tx_empty());
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}
