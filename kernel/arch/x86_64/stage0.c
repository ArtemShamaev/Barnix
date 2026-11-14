/* Stage 0 of the x86_64 port: prove the long-mode boot path on Legacy BIOS
 * and UEFI. Prints the Multiboot memory map to the VGA text screen. */
#include <stdint.h>
#include "multiboot.h"

__attribute__((section(".multiboot"), used))
const unsigned int multiboot_header[] = { 0x1BADB002, 0, -(0x1BADB002) };

static volatile uint16_t *const vga = (volatile uint16_t *)0xb8000;
static int row, column;

static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
/* COM1 mirrors the console: UEFI machines have no VGA text mode. */
static void serial_put(char c)
{
    for (int i = 0; i < 100000 && !(inb(0x3fd) & 0x20); i++) {}
    outb(0x3f8, (uint8_t)c);
}
static void put(char c)
{
    serial_put(c);
    if (c == '\n') { row++; column = 0; return; }
    if (row >= 25) return;
    vga[row * 80 + column] = 0x0f00 | (unsigned char)c;
    if (++column == 80) { row++; column = 0; }
}
static void text(const char *s) { while (*s) put(*s++); }
static void hex(uint64_t value)
{
    text("0x");
    for (int shift = 60; shift >= 0; shift -= 4) put("0123456789abcdef"[(value >> shift) & 15]);
}

void kernel_main(uint32_t magic, const MultibootInfo *info)
{
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 1); outb(0x3f9, 0);   /* 115200 baud */
    outb(0x3fb, 3); outb(0x3fa, 0xc7);                                   /* 8N1, FIFO */
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0f20;
    text("Barnix x86_64: long mode OK\n");
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    text("stack "); hex(rsp); text(", pointer size "); put('0' + (int)sizeof(void *)); text("\n");
    if (magic != MULTIBOOT_BOOT_MAGIC) { text("bad Multiboot magic "); hex(magic); text("\n"); return; }
    text("memory: lower "); hex(info->mem_lower); text(" KiB, upper "); hex(info->mem_upper); text(" KiB\n");
    if (info->flags & (1U << 6)) {
        uint64_t total = 0;
        for (uint32_t offset = 0; offset < info->mmap_length;) {
            const uint32_t *record = (const uint32_t *)(uintptr_t)(info->mmap_addr + offset);
            uint64_t base = record[1] | (uint64_t)record[2] << 32, size = record[3] | (uint64_t)record[4] << 32;
            text("  "); hex(base); text(" + "); hex(size); text(record[5] == 1 ? " usable\n" : " reserved\n");
            if (record[5] == 1) total += size;
            offset += record[0] + 4;
        }
        text("usable RAM: "); hex(total >> 20); text(" MiB\n");
    }
    text("stage 0 done\n");
}
