/* CPU exceptions outside Linux programs: the kernel and native programs run
 * in ring 0 with these gates installed, so a fault shows a KERNEL PANIC with
 * its cause instead of a silent reboot (triple fault). */
#include <stdint.h>
#include "fault.h"
#include "barnix.h"
#include "smp.h"
#include "app_abi.h"
#include "linux_tss.h"

extern void (*const fault_stubs[32])(void);
extern void fault_double_entry(void);
extern uint8_t fault_double_stack_top[];
extern uint64_t gdt_fault_tss_descriptor;

#define FAULT_TSS_SELECTOR 0x38   /* GDT entry 7, see boot.S */

static uint64_t idt[32];
static struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idtr;
static uint32_t double_tss[26] __attribute__((aligned(16)));   /* 32-bit TSS, 104 bytes */

static const char *const names[32] = {
    "#DE divide error", "#DB debug", "NMI", "#BP breakpoint", "#OF overflow",
    "#BR bound range", "#UD invalid instruction", "#NM no FPU", "#DF double fault",
    "coprocessor overrun", "#TS invalid TSS", "#NP segment not present",
    "#SS stack fault", "#GP general protection", "#PF page fault", "reserved",
    "#MF x87 error", "#AC alignment check", "#MC machine check", "#XM SSE error",
    "#VE virtualization", "#CP control protection",
};

static uint64_t interrupt_gate(void (*handler)(void))
{
    uint32_t address = (uint32_t)(uintptr_t)handler;
    return (address & 0xffffU) | 0x08U << 16 |
           (uint64_t)(0x8e00U | (address & 0xffff0000U)) << 32;   /* present, ring 0 */
}

void fault_fill_idt(uint64_t *table, void (*nmi)(void))
{
    for (unsigned int i = 0; i < 32; i++) table[i] = interrupt_gate(fault_stubs[i]);
    if (nmi) table[2] = interrupt_gate(nmi);
    table[8] = (uint64_t)FAULT_TSS_SELECTOR << 16 | (uint64_t)0x8500U << 32;   /* task gate */
}

void fault_init(void)
{
    double_tss[8] = (uint32_t)(uintptr_t)fault_double_entry;        /* eip */
    double_tss[9] = 0x2;                                            /* eflags */
    double_tss[14] = (uint32_t)(uintptr_t)fault_double_stack_top;   /* esp */
    /* 32-bit TSS words: 18 es, 19 cs, 20 ss, 21 ds, 22 fs, 23 gs, 24 ldt. */
    double_tss[19] = 0x08;
    double_tss[18] = double_tss[20] = double_tss[21] = double_tss[22] = double_tss[23] = 0x10;
    double_tss[25] = (uint32_t)sizeof(double_tss) << 16;           /* no I/O map */
    uint32_t base = (uint32_t)(uintptr_t)double_tss, limit = sizeof(double_tss) - 1;
    gdt_fault_tss_descriptor = (limit & 0xffffU) | (uint64_t)(base & 0xffffffU) << 16 |
                               (uint64_t)0x89U << 40 | (uint64_t)(base >> 24) << 56;
    /* A task switch (the double-fault gate) saves the current state into the
     * task register's TSS: give the boot processor a valid one now. */
    linux_tss_init();
    fault_fill_idt(idt, 0);
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)(uintptr_t)idt;
    fault_load();
}

/* ---- stack guard for native programs ----
 * Programs run in ring 0 and, without paging, an overflowing stack silently
 * overwrites the program and then the kernel (even this IDT), ending in a
 * triple fault. While a program runs, paging maps all 4 GiB onto itself with
 * 4 MiB pages; only the 4 KiB page below the program's stack is absent, so
 * an overflow faults at once and the double-fault task reports it. */
static uint32_t guard_directory[1024] __attribute__((aligned(4096)));
static uint32_t guard_table[1024] __attribute__((aligned(4096)));
#define GUARD_PAGE (APP_LOAD_END - 4096U)

uint32_t fault_guard_enter(uint32_t image_end)
{
    if (!guard_directory[0]) {
        for (uint32_t i = 0; i < 1024; i++) guard_directory[i] = i << 22 | 0x83U;   /* 4 MiB, rw */
        guard_directory[APP_LOAD_MIN >> 22] = (uint32_t)(uintptr_t)guard_table | 0x3U;
        double_tss[7] = (uint32_t)(uintptr_t)guard_directory;                        /* cr3 */
    }
    for (uint32_t j = 0; j < 1024; j++) guard_table[j] = ((APP_LOAD_MIN & 0xffc00000U) + (j << 12)) | 0x3U;
    /* A program whose image reaches into that page keeps it (no guard). */
    if (image_end <= GUARD_PAGE) guard_table[(GUARD_PAGE >> 12) & 1023] = 0;
    uint32_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0; mov %%cr4, %1" : "=r"(cr0), "=r"(cr4));
    __asm__ volatile("mov %0, %%cr4; mov %1, %%cr3; mov %2, %%cr0" ::
                     "r"(cr4 | 0x10U), "r"((uint32_t)(uintptr_t)guard_directory), "r"(cr0 | 0x80000000U) : "memory");
    return cr0;
}

void fault_guard_leave(uint32_t previous_cr0)
{
    __asm__ volatile("mov %0, %%cr0" :: "r"(previous_cr0) : "memory");
}

void fault_load(void)
{
    __asm__ volatile("lidt %0" :: "m"(idtr));
}

/* ---- the report ---- */
static char report[640];
static unsigned int length;
static volatile unsigned int ap_failed;

static void add(const char *text) { while (*text && length + 1 < sizeof(report)) report[length++] = *text++; report[length] = 0; }
static void add_hex(uint32_t value)
{
    char digits[11] = "0x";
    for (int i = 0; i < 8; i++) digits[2 + i] = "0123456789abcdef"[(value >> (28 - 4 * i)) & 15];
    digits[10] = 0;
    add(digits);
}
static void add_number(uint32_t value)
{
    char digits[11]; int n = 0;
    do digits[n++] = (char)('0' + value % 10); while ((value /= 10) && n < 10);
    while (n) { char c[2] = {digits[--n], 0}; add(c); }
}
static void emit_debug(void)
{
    /* QEMU debug console and COM1, for logs and automated tests. */
    for (unsigned int i = 0; i < length; i++) {
        __asm__ volatile("outb %0, %1" :: "a"(report[i]), "Nd"((uint16_t)0xe9));
        __asm__ volatile("outb %0, %1" :: "a"(report[i]), "Nd"((uint16_t)0x3f8));
    }
}

static void describe(uint32_t vector, uint32_t error, uint32_t eip, uint32_t esp, int on_ap)
{
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    length = 0;
    add("CPU exception "); add(vector < 32 && names[vector] ? names[vector] : "?");
    add(" (vector "); add_number(vector); add("), error "); add_hex(error); add("\n");
    add(eip < 0x01000000U ? "In the kernel" : "In a program");
    add(on_ap ? " on an extra processor core (parallel job)" : "");
    add(", EIP "); add_hex(eip); add(", ESP "); add_hex(esp);
    if (vector == 14) { add(", address "); add_hex(cr2); }
    add("\n");
    /* Programs run without paging: an overflowing stack silently overwrites
     * the program below it until something faults. Name the likely cause. */
    if (!on_ap && ((esp < APP_LOAD_END && esp >= APP_LOAD_MIN) ||
                   (vector == 14 && cr2 >= GUARD_PAGE && cr2 < APP_LOAD_END)))
        add("Likely cause: the program's stack overflowed (it is 64 KiB) and\n"
            "overwrote the program's own code or data.\n");
}

void fault_report(const uint32_t *frame)
{
    /* frame: edi esi ebp esp ebx edx ecx eax, vector, error, eip, cs, eflags */
    uint32_t esp = (uint32_t)(uintptr_t)(frame + 13);   /* ring 0: no stack switch */
    int on_ap = smp_on_extra_core(esp);
    describe(frame[8], frame[9], frame[10], esp, on_ap);
    static const struct { const char *name; int index; } regs[7] = {
        {"EAX ", 7}, {" ECX ", 6}, {" EDX ", 5}, {" EBX ", 4}, {" ESI ", 1}, {" EDI ", 0}, {" EBP ", 2}};
    for (int i = 0; i < 7; i++) { add(regs[i].name); add_hex(frame[regs[i].index]); }
    add("\nEFLAGS "); add_hex(frame[12]); add("\n");
    emit_debug();
    if (on_ap) {
        /* The boot processor shows it: it is waiting for this core's job. */
        ap_failed = 1;
        return;
    }
    panic(report);
}

void fault_double_report(void)
{
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    length = 0;
    if (cr2 >= GUARD_PAGE && cr2 < APP_LOAD_END)
        add("CPU exception #DF double fault\nIn a program: its stack overflowed (it is 64 KiB);\n"
            "Likely cause: the program's stack overflowed into the guard page.\n");
    else
        add("CPU exception #DF double fault: a fault while handling another fault,\n"
            "usually a stack overflow in the kernel or in a program.\n");
    emit_debug();
    panic(report);
}

/* Called by the boot processor while it waits for parallel jobs. */
void fault_check_extra_cores(void)
{
    if (!ap_failed) return;
    panic(report);
}
