/* x86_64 processor tables: the TSS (ring-0 stack for programs, a separate
 * stack for double faults) and the IDT. Every CPU exception is reported with
 * its cause, RIP, CR2 and registers on COM1 and the console, then the CPU
 * halts: no silent triple-fault reboot. */
#include "cpu.h"

typedef struct {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3, iomap;
} __attribute__((packed)) Tss64;
_Static_assert(sizeof(Tss64) == 104, "64-bit TSS size");

typedef struct {
    uint16_t offset_low, selector;
    uint8_t ist, flags;
    uint16_t offset_middle;
    uint32_t offset_high, reserved;
} __attribute__((packed)) Gate;

extern uint64_t gdt64_tss[2];
extern void (*const cpu_stubs[32])(void);

static Tss64 tss __attribute__((aligned(16)));
static Gate idt[256] __attribute__((aligned(16)));
static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));
void (*cpu_report_output)(const char *text);

void cpu_set_gate(unsigned int vector, void (*handler)(void), unsigned int dpl)
{
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector] = (Gate){(uint16_t)address, SEL_KERNEL_CODE, 0, (uint8_t)(0x8e | (dpl & 3) << 5),
                         (uint16_t)(address >> 16), (uint32_t)(address >> 32), 0};
}

void cpu_set_kernel_stack(uint64_t top) { tss.rsp[0] = top; }

void cpu_init(void)
{
    tss.ist[0] = (uint64_t)(uintptr_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.iomap = sizeof(tss);   /* no I/O permission map */
    uint64_t base = (uint64_t)(uintptr_t)&tss, limit = sizeof(tss) - 1;
    gdt64_tss[0] = (limit & 0xffff) | (base & 0xffffff) << 16 | (uint64_t)0x89 << 40 |
                   ((limit >> 16) & 0xf) << 48 | ((base >> 24) & 0xff) << 56;
    gdt64_tss[1] = base >> 32;
    __asm__ volatile("ltr %w0" :: "r"(SEL_TSS) : "memory");

    for (unsigned int i = 0; i < 32; i++) cpu_set_gate(i, cpu_stubs[i], 0);
    idt[8].ist = 1;   /* double fault: always a fresh stack */
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr = {sizeof(idt) - 1, (uint64_t)(uintptr_t)idt};
    __asm__ volatile("lidt %0" :: "m"(idtr) : "memory");
}

/* ---- the report ---- */
static const char *const names[32] = {
    "#DE divide error", "#DB debug", "NMI", "#BP breakpoint", "#OF overflow",
    "#BR bound range", "#UD invalid instruction", "#NM no FPU", "#DF double fault",
    "coprocessor overrun", "#TS invalid TSS", "#NP segment not present",
    "#SS stack fault", "#GP general protection", "#PF page fault", "reserved",
    "#MF x87 error", "#AC alignment check", "#MC machine check", "#XM SSE error",
    "#VE virtualization", "#CP control protection",
};
static char report[1024];
static unsigned int length;

static void add(const char *text) { while (*text && length + 1 < sizeof(report)) report[length++] = *text++; report[length] = 0; }
static void add_hex(uint64_t value)
{
    char digits[19] = "0x";
    for (int i = 0; i < 16; i++) digits[2 + i] = "0123456789abcdef"[(value >> (60 - 4 * i)) & 15];
    digits[18] = 0;
    add(digits);
}
static void add_number(uint64_t value)
{
    char digits[21]; int n = 0;
    do digits[n++] = (char)('0' + value % 10); while ((value /= 10) && n < 20);
    while (n) { char c[2] = {digits[--n], 0}; add(c); }
}
static inline uint8_t inb(uint16_t port) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port)); }

void cpu_exception(const CpuFrame *f)
{
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    length = 0;
    add("\nKERNEL PANIC\nCPU exception ");
    add(f->vector < 32 && names[f->vector] ? names[f->vector] : "?");
    add(" (vector "); add_number(f->vector); add("), error "); add_hex(f->error); add("\n");
    add((f->cs & 3) ? "In a user program" : "In the kernel or a Barnix program");
    add(", RIP "); add_hex(f->rip); add(", RSP "); add_hex(f->rsp);
    if (f->vector == 14) { add(", address "); add_hex(cr2); }
    add("\nRAX "); add_hex(f->rax); add(" RBX "); add_hex(f->rbx); add(" RCX "); add_hex(f->rcx);
    add("\nRDX "); add_hex(f->rdx); add(" RSI "); add_hex(f->rsi); add(" RDI "); add_hex(f->rdi);
    add("\nRBP "); add_hex(f->rbp); add(" RFLAGS "); add_hex(f->rflags); add("\nSystem halted.\n");
    for (unsigned int i = 0; i < length; i++) {
        for (int spin = 0; spin < 100000 && !(inb(0x3fd) & 0x20); spin++) {}
        outb(0x3f8, (uint8_t)report[i]);
        outb(0xe9, (uint8_t)report[i]);
    }
    if (cpu_report_output) cpu_report_output(report);
}
