/* Application processors as compute workers. They are woken once with the
 * INIT/SIPI/SIPI broadcast, count themselves in, and then wait for jobs. */
#include "smp.h"
#include "memory.h"
#include "barnix.h"
#include "usb_time.h"
#include "fault.h"

#define SMP_MAX 16
#define STACK_BYTES 16384U
#define SPIN_BEFORE_SLEEP 200000U

typedef struct {
    unsigned int arrived, max, stack_bytes, stacks, entry;
    unsigned short gdt_limit;
    unsigned int gdt_base;
} __attribute__((packed)) TrampolineFields;

extern const unsigned char smp_trampoline[], smp_trampoline_end[];
extern char smp_tr_base[], smp_tr_gdt[], smp_tr_far[], smp_tr_protected[], smp_tr_gdt_offset[], smp_tr_fields[];
extern void smp_idle(volatile unsigned int *generation, unsigned int seen);
extern void smp_nmi(void);

static volatile unsigned int *lapic;
static int x2apic;
static TrampolineFields *fields;
/* ticket: the job generation (high 16 bits) and the next index to claim, so a
 * late core can never take an index of a newer job. */
static volatile unsigned int started, generation, ticket, done, total, sleeping[SMP_MAX];
static void (*volatile job)(void *, unsigned int);
static void *volatile job_argument;
static unsigned long long ap_idt[32];
static unsigned int stacks_base;   /* extra cores' stacks, for fault reports */
static struct { unsigned short limit; unsigned int base; } __attribute__((packed)) ap_idtr;

static unsigned int atomic_add(volatile unsigned int *value, unsigned int add)
{
    __asm__ volatile("lock xaddl %0,%1" : "+r"(add), "+m"(*value) :: "memory");
    return add;
}
static int atomic_swap_if(volatile unsigned int *value, unsigned int old, unsigned int to)
{
    unsigned int seen;
    __asm__ volatile("lock cmpxchgl %2,%1" : "=a"(seen), "+m"(*value) : "r"(to), "0"(old) : "memory");
    return seen == old;
}
static void atomic_set(volatile unsigned int *value, unsigned int to)
{
    __asm__ volatile("xchgl %0,%1" : "+r"(to), "+m"(*value) :: "memory");
}
static void cpuid(unsigned int leaf, unsigned int *a, unsigned int *b, unsigned int *c, unsigned int *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}
static int has_cpuid(void)
{
    unsigned int a, b;
    __asm__ volatile("pushfl; pushfl; popl %0; movl %0,%1; xorl $0x200000,%0; pushl %0; popfl; pushfl; popl %0; popfl"
                     : "=&r"(a), "=&r"(b));
    return ((a ^ b) & 0x200000) != 0;
}
static unsigned long long rdmsr(unsigned int msr)
{
    unsigned int lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return lo | (unsigned long long)hi << 32;
}
static void wrmsr(unsigned int msr, unsigned long long value)
{
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((unsigned int)value), "d"((unsigned int)(value >> 32)));
}

/* Interrupt command to every processor but this one. */
static void ipi_others(unsigned int command)
{
    command |= 3U << 18;
    if (x2apic) { wrmsr(0x830, command); return; }
    for (unsigned int limit = 1000000; (lapic[0x300 / 4] & (1U << 12)) && --limit;) __asm__ volatile("pause");
    lapic[0x310 / 4] = 0;
    lapic[0x300 / 4] = command;
    for (unsigned int limit = 1000000; (lapic[0x300 / 4] & (1U << 12)) && --limit;) __asm__ volatile("pause");
}

static void run_jobs(unsigned int tag)
{
    for (;;) {
        unsigned int claim = ticket, i = claim & 0xffff;
        if (claim >> 16 != tag || i >= total) return;
        if (!atomic_swap_if(&ticket, claim, claim + 1)) continue;
        job(job_argument, i);
        atomic_add(&done, 1);
    }
}

static void ap_main(unsigned int index)
{
    __asm__ volatile("lidt %0" :: "m"(ap_idtr));
    /* INIT leaves CR0.CD/NW set: without this the core runs with its caches
     * disabled. Match the boot processor: caches on, x87 and SSE enabled. */
    unsigned int cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(0x60000000U | 0x4U | 0x8U);   /* CD, NW, EM, TS */
    cr0 |= 0x2U;                           /* MP */
    __asm__ volatile("mov %0, %%cr0; wbinvd" :: "r"(cr0) : "memory");
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x600U;                         /* OSFXSR, OSXMMEXCPT */
    __asm__ volatile("mov %0, %%cr4; fninit" :: "r"(cr4) : "memory");
    atomic_add(&started, 1);
    unsigned int seen = generation, spins = 0;
    for (;;) {
        if (generation != seen) {
            seen = generation;
            run_jobs(seen & 0xffff);
            spins = 0;
            continue;
        }
        if (++spins < SPIN_BEFORE_SLEEP) { __asm__ volatile("pause"); continue; }
        atomic_set(&sleeping[index], 1);
        smp_idle(&generation, seen);
        atomic_set(&sleeping[index], 0);
        spins = 0;
    }
}

/* The highest free page below the EBDA for the real-mode entry. */
static unsigned int trampoline_page(void)
{
    unsigned int bytes = (unsigned int)(smp_trampoline_end - smp_trampoline);
    for (unsigned int page = 0x9e000; page >= 0x8000; page -= 4096)
        if (memory_low_free(page, bytes)) return page;
    return 0;
}

void smp_init(void)
{
    started = 0;
    if (!has_cpuid()) return;
    unsigned int a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    if (a < 1) return;
    cpuid(1, &a, &b, &c, &d);
    if (!(d & (1U << 9)) || !(d & (1U << 5))) return;   /* local APIC, MSRs */
    unsigned long long base = rdmsr(0x1b);
    if (!(base & (1U << 11))) return;                    /* APIC disabled by firmware */
    x2apic = (base & (1U << 10)) != 0;
    if (!x2apic) {
        if (base >> 32) return;
        lapic = (volatile unsigned int *)(unsigned int)(base & 0xfffff000U);
        lapic[0xf0 / 4] |= 0x1ff;                         /* software enable, spurious 0xff */
    } else wrmsr(0x80f, rdmsr(0x80f) | 0x1ff);
    unsigned int page = trampoline_page();
    void *stacks = memory_kernel_alloc(SMP_MAX * STACK_BYTES);
    stacks_base = (unsigned int)stacks;
    if (!page || !stacks) return;

    /* CPU exceptions report a KERNEL PANIC; vector 2 (NMI) wakes the core. */
    fault_fill_idt((uint64_t *)ap_idt, smp_nmi);
    ap_idtr.limit = sizeof(ap_idt) - 1; ap_idtr.base = (unsigned int)ap_idt;

    unsigned char *code = (unsigned char *)page;
    memcpy(code, smp_trampoline, (unsigned int)(smp_trampoline_end - smp_trampoline));
    *(unsigned int *)(code + (unsigned int)smp_tr_base) = page;
    *(unsigned int *)(code + (unsigned int)smp_tr_gdt) = page + (unsigned int)smp_tr_gdt_offset;
    *(unsigned int *)(code + (unsigned int)smp_tr_far) = page + (unsigned int)smp_tr_protected;
    fields = (TrampolineFields *)(code + (unsigned int)smp_tr_fields);
    fields->arrived = 0; fields->max = SMP_MAX - 1; fields->stack_bytes = STACK_BYTES;
    fields->stacks = (unsigned int)stacks; fields->entry = (unsigned int)ap_main;
    struct { unsigned short limit; unsigned int base; } __attribute__((packed)) gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    fields->gdt_limit = gdtr.limit; fields->gdt_base = gdtr.base;

    ipi_others(0x4500);                   /* INIT, level assert */
    usb_delay_ms(10);
    for (int i = 0; i < 2; i++) {         /* START-UP at page >> 12 */
        ipi_others(0x4600 | page >> 12);
        usb_delay_ms(1);
    }
    /* Wait until the cores that answered have left the trampoline. */
    for (int ms = 0; ms < 100; ms++) {
        usb_delay_ms(1);
        unsigned int arrived = fields->arrived > SMP_MAX - 1 ? SMP_MAX - 1 : fields->arrived;
        if (ms >= 20 && arrived && started == arrived) break;
    }
}

unsigned int smp_cpus(void) { return started + 1; }

int smp_on_extra_core(unsigned int esp)
{
    return stacks_base && esp >= stacks_base && esp - stacks_base < SMP_MAX * STACK_BYTES;
}

void smp_parallel(void (*fn)(void *, unsigned int), void *argument, unsigned int count)
{
    if (!fn || !count) return;
    if (!started || count == 1 || count > 0x8000) {
        for (unsigned int i = 0; i < count; i++) fn(argument, i);
        return;
    }
    unsigned int tag = (generation + 1) & 0xffff;
    job = fn; job_argument = argument; total = count;
    atomic_set(&done, 0);
    atomic_set(&ticket, tag << 16);
    atomic_add(&generation, 1);
    int wake = 0;
    for (unsigned int i = 0; i < SMP_MAX; i++) if (sleeping[i]) wake = 1;
    if (wake) ipi_others(0x0400);         /* NMI */
    run_jobs(tag);
    while (done < count) { fault_check_extra_cores(); __asm__ volatile("pause"); }
}
