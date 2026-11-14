/* Stage 0 of the x86_64 port: prove the long-mode boot path on Legacy BIOS
 * and UEFI. Prints the Multiboot memory map to the VGA text screen. */
#include <stdint.h>
#include "multiboot.h"
#include "cpu.h"

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


static void vga_report(const char *report)
{
    row = 13; column = 0;
    for (const char *p = report; *p; p++) {
        if (*p == '\n') { row++; column = 0; continue; }
        if (row < 25) vga[row * 80 + column] = 0x4f00 | (unsigned char)*p;
        if (++column == 80) { row++; column = 0; }
    }
}

void kernel_main(uint32_t magic, const MultibootInfo *info)
{
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 1); outb(0x3f9, 0);   /* 115200 baud */
    outb(0x3fb, 3); outb(0x3fa, 0xc7);                                   /* 8N1, FIFO */
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0f20;
    cpu_init();
    cpu_report_output = vga_report;
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

    /* Exception self-test: "fault=div|gp|pf|stack" on the GRUB command line. */
    const char *line = (info->flags & MULTIBOOT_CMDLINE) ? (const char *)(uintptr_t)info->command_line : "";
    const char *fault = 0;
    for (const char *p = line; *p; p++)
        if (p[0] == 'f' && p[1] == 'a' && p[2] == 'u' && p[3] == 'l' && p[4] == 't' && p[5] == '=') fault = p + 6;
    if (!fault) return;
    text("triggering fault: "); text(fault); text("\n");
    if (fault[0] == 'd') __asm__ volatile("xor %%ecx, %%ecx; mov $1, %%eax; xor %%edx, %%edx; div %%ecx" ::: "rax", "rcx", "rdx");
    if (fault[0] == 'g') { volatile uint64_t *bad = (volatile uint64_t *)0x8000000000000000ULL; (void)*bad; }
    if (fault[0] == 'p') { volatile uint64_t *far = (volatile uint64_t *)0x100000000000ULL; (void)*far; }
    /* A stack in unmapped memory: the #PF cannot be pushed, so the double
     * fault must arrive on its own IST stack. */
    if (fault[0] == 's') __asm__ volatile("mov $0x100000000000, %%rsp; push %%rax" ::: "memory");
    text("fault did not trigger\n");
}
