#include "timer.h"

static unsigned int tsc_per_ms;
static int calibrated;

static inline unsigned char port_in(unsigned short p) { unsigned char v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void port_out(unsigned short p, unsigned char v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p) : "memory"); }
static inline unsigned int tsc_low(void) { unsigned int lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return lo; }
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

/* One PIT channel 2 one-shot of count ticks; 0 when it finished, -1 when the
 * gate never rose (clock-gated 8254). guard bounds the wait in port reads. */
static int pit_wait(unsigned int count, unsigned int guard)
{
    unsigned char old = port_in(0x61);
    port_out(0x61, old & ~3U);
    port_out(0x43, 0xb0);
    port_out(0x42, count & 255);
    port_out(0x42, count >> 8);
    port_out(0x61, (old & ~2U) | 1U);
    while (!(port_in(0x61) & 32) && --guard) {}
    port_out(0x61, old);
    return guard ? 0 : -1;
}

static void calibrate(void)
{
    calibrated = 1;
    unsigned int a, b, c, d, max;
    if (!has_cpuid()) return;
    cpuid(0, &max, &b, &c, &d);
    cpuid(1, &a, &b, &c, &d);
    if (!(d & 16)) return;                                 /* no TSC */
    /* Leaf 15h: TSC = crystal * ebx / eax, exact where the crystal is given. */
    if (max >= 0x15) {
        cpuid(0x15, &a, &b, &c, &d);
        if (a && b && c) { tsc_per_ms = c / 1000 * b / a; if (tsc_per_ms) return; }
    }
    /* Measure 10 ms of PIT; about 2 s of port reads at most if it is gated. */
    unsigned int start = tsc_low();
    if (!pit_wait(11932, 2000000)) { tsc_per_ms = (tsc_low() - start) / 10; if (tsc_per_ms > 1000) return; }
    /* Leaf 16h: base frequency in MHz, close to the TSC on Intel. */
    if (max >= 0x16) { cpuid(0x16, &a, &b, &c, &d); if (a & 0xffff) { tsc_per_ms = (a & 0xffff) * 1000; return; } }
    tsc_per_ms = 2000000;                                  /* a guess: 2 GHz */
}

unsigned int timer_tsc_per_ms(void)
{
    if (!calibrated) calibrate();
    return tsc_per_ms;
}

static void tsc_wait(unsigned int cycles)
{
    unsigned int start = tsc_low();
    while (tsc_low() - start < cycles) __asm__ volatile("pause");
}

void timer_delay_ms(unsigned int ms)
{
    unsigned int per_ms = timer_tsc_per_ms();
    if (!per_ms) {   /* no TSC: old CPUs have a working PIT */
        while (ms) { unsigned int part = ms > 20 ? 20 : ms; pit_wait(1193 * part + 1, 10000000); ms -= part; }
        return;
    }
    while (ms--) tsc_wait(per_ms);
}

void timer_delay_us(unsigned int us)
{
    unsigned int per_ms = timer_tsc_per_ms();
    if (!per_ms) { pit_wait(us * 1193 / 1000 + 1, 10000000); return; }
    while (us >= 1000) { tsc_wait(per_ms); us -= 1000; }
    tsc_wait(per_ms / 1000 * us);
}
