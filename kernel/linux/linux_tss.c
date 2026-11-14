#include "linux_tss.h"
#include "linux_memory.h"
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
extern uint64_t gdt_tls_descriptor;

uint64_t linux_tss_tls_get(void) { return gdt_tls_descriptor; }
void linux_tss_tls_set(uint64_t descriptor) { gdt_tls_descriptor = descriptor; }

int linux_tss_set_thread_area(unsigned int address)
{
    if (!linux_user_range(address, 16)) return -14; /* EFAULT */
    uint32_t *user = (uint32_t *)address;
    uint32_t entry = user[0], base = user[1], limit = user[2], flags = user[3];
    if (entry != 0xffffffffU && entry != 6) return -22;
    if (flags & ~0x7fU) return -22;
    unsigned int contents = (flags >> 1) & 3U;
    if (contents == 3) return -22;
    if (limit > 0xfffffU) return -22;
    /* Access byte: present, ring 3, code/data, then Linux user_desc type. */
    uint32_t access = 0xf0U | ((contents & 2U) << 2) |
                      ((contents & 1U) << 2) | ((flags & 8U) ? 0 : 2U);
    if (flags & 32U) access &= ~0x80U;
    uint32_t granularity = ((flags & 1U) ? 0x40U : 0) |
                           ((flags & 16U) ? 0x80U : 0) |
                           ((flags & 64U) ? 0x10U : 0);
    gdt_tls_descriptor = (uint64_t)(limit & 0xffffU) |
        ((uint64_t)(base & 0xffffffU) << 16) |
        ((uint64_t)access << 40) |
        ((uint64_t)(((limit >> 16) & 15U) | granularity) << 48) |
        ((uint64_t)(base >> 24) << 56);
    user[0] = 6;
    return 0;
}

void linux_tss_init(void)
{
    static int initialized;
    if (initialized) return;
    gdt_tls_descriptor = 0;
    uint32_t base = (uint32_t)&tss, limit = sizeof(tss) - 1;
    gdt_tss_descriptor = (uint64_t)limit | ((uint64_t)(base & 0xFFFFFF) << 16) |
        ((uint64_t)0x89 << 40) | ((uint64_t)(base >> 24) << 56);
    tss.ss0 = 0x10;
    tss.esp0 = (uint32_t)(syscall_stack + sizeof(syscall_stack));
    tss.iomap = sizeof(tss); /* No user I/O permissions. */
    __asm__ volatile("ltr %%ax" : : "a"((uint16_t)0x28) : "memory");
    initialized = 1;
}
