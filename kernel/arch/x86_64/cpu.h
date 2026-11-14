#ifndef ARCH_X86_64_CPU_H
#define ARCH_X86_64_CPU_H
#include <stdint.h>
/* GDT selectors of the x86_64 kernel (see boot.S). */
#define SEL_KERNEL_CODE   0x08
#define SEL_KERNEL_DATA   0x10
#define SEL_USER_CODE32   0x1b
#define SEL_USER_DATA     0x23
#define SEL_USER_CODE64   0x2b
#define SEL_LINUX_TLS     0x33
#define SEL_KERNEL_CODE32 0x38
#define SEL_TSS           0x40

/* Saved by cpu_entry.S, lowest address first. */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
} CpuFrame;

/* GDT TSS, IDT with exception gates (double fault on its own stack). */
void cpu_init(void);
/* Install a gate: dpl 0 for the kernel, 3 for int 0x80 from programs. */
void cpu_set_gate(unsigned int vector, void (*handler)(void), unsigned int dpl);
/* Kernel stack used when a ring-3 program enters the kernel. */
void cpu_set_kernel_stack(uint64_t top);
/* Console output for reports: set by the kernel (VGA, framebuffer). */
extern void (*cpu_report_output)(const char *text);
#endif
