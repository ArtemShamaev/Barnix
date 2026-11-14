#include <stdint.h>
#include "linux_idt.h"
#include "linux_abi.h"
#include "linux_exec.h"
#include "linux_task.h"
#include "linux_segments.h"
#include "barnix.h"

typedef struct { unsigned short low, selector; unsigned char zero, flags; unsigned short high; } __attribute__((packed)) IdtGate;
typedef struct { unsigned short limit; unsigned int address; } __attribute__((packed)) IdtPointer;
static IdtGate idt[256];
static IdtPointer previous_idt;
extern void linux_user_fault(void);
extern void linux_user_fault_error(void);
extern void linux_int80(void);
extern void (*const linux_fault_stubs[32])(void);
extern unsigned int linux_fault_vector;

static void set_gate(unsigned int vector, void (*handler)(void), unsigned char flags)
{
    unsigned int address = (unsigned int)(uintptr_t)handler;
    idt[vector].low = address; idt[vector].selector = 0x08;
    idt[vector].zero = 0; idt[vector].flags = flags; /* present, DPL3, interrupt gate */
    idt[vector].high = address >> 16;
}
void linux_idt_init(void)
{
    for (unsigned int i = 0; i < 256; i++) { idt[i].low = 0; idt[i].selector = 0; idt[i].zero = 0; idt[i].flags = 0; idt[i].high = 0; }
    __asm__ volatile("sidtl %0" : "=m"(previous_idt));
    for (unsigned int i = 0; i < 32; i++) set_gate(i, linux_fault_stubs[i], 0x8E);
    set_gate(0x80, linux_int80, 0xEE);
    IdtPointer pointer = {(unsigned short)(sizeof(idt) - 1), (unsigned int)(uintptr_t)idt};
    __asm__ volatile("lidtl %0" : : "m"(pointer));
}

void linux_syscall_from_regs(LinuxTrapFrame *regs)
{
    if (linux_task_syscall(regs)) return;
    LinuxSyscallFrame frame = {regs->eax, regs->ebx, regs->ecx, regs->edx,
                               regs->esi, regs->edi, regs->ebp};
    int result = linux_syscall_dispatch(&frame);
    if (result == LINUX_IO_WAIT || result == LINUX_IO_SIGPIPE) {
        linux_task_io_result(regs, result);
        return;
    }
    regs->eax = (unsigned int)result;
    linux_exec_apply(regs);
}

void linux_idt_restore(void)
{
    __asm__ volatile("lidtl %0" : : "m"(previous_idt) : "memory");
}

/* A fault in the kernel while a Linux program runs: where it happened. */
void linux_kernel_fault(const unsigned int *frame)
{
    static char text[120];
    static const char hex[] = "0123456789abcdef";
    unsigned int cr2, values[4] = {linux_fault_vector, frame[1], frame[0], 0};
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    values[3] = cr2;
    const char *names[4] = {" vector=", " eip=", " error=", " cr2="};
    const char *head = "Fault in Linux ABI kernel handler";
    int n = 0;
    while (*head) text[n++] = *head++;
    for (int i = 0; i < 4; i++) {
        for (const char *t = names[i]; *t; t++) text[n++] = *t;
        for (int shift = 28; shift >= 0; shift -= 4) text[n++] = hex[(values[i] >> shift) & 15];
    }
    text[n] = 0;
    for (int i = 0; i < n; i++) __asm__ volatile("outb %0, %1" : : "a"(text[i]), "Nd"((unsigned short)0xe9));
    panic(text);
}
