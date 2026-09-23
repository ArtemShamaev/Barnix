#include "linux_tss.h"
#include <stddef.h>
#include <stdint.h>

/* Hardware i386 TSS: segment selectors occupy four bytes including padding. */
typedef struct {
    uint32_t link, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap;
} __attribute__((packed)) LinuxTSS;
_Static_assert(sizeof(LinuxTSS) == 104, "i386 TSS size");
_Static_assert(offsetof(LinuxTSS, esp0) == 4, "TSS esp0 offset");
_Static_assert(offsetof(LinuxTSS, iomap) == 102, "TSS I/O map offset");
static LinuxTSS tss __attribute__((aligned(16)));
/* A syscall must never overwrite the suspended caller's kernel stack. */
static unsigned char syscall_stack[32768] __attribute__((aligned(16)));
extern uint64_t gdt_tss_descriptor;

void linux_tss_init(void)
{
    static int initialized;
    if (initialized) return;
    uint32_t base = (uint32_t)&tss, limit = sizeof(tss) - 1;
    gdt_tss_descriptor = (uint64_t)limit | ((uint64_t)(base & 0xFFFFFF) << 16) |
        ((uint64_t)0x89 << 40) | ((uint64_t)(base >> 24) << 56);
    tss.ss0 = 0x10;
    tss.esp0 = (uint32_t)(syscall_stack + sizeof(syscall_stack));
    tss.iomap = sizeof(tss); /* No user I/O permissions. */
    __asm__ volatile("ltr %%ax" : : "a"((uint16_t)0x28) : "memory");
    initialized = 1;
}
