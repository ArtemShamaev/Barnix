/* Deliberate CPU exceptions for tests/fault_test.py: the kernel must show a
 * KERNEL PANIC with the cause instead of rebooting. */
#include "command.h"

static volatile int zero;
static int same(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
static int divide(void) { return 100 / zero; }
#pragma GCC diagnostic ignored "-Winfinite-recursion"
static int recurse(volatile int depth) { volatile char pad[256]; pad[0] = (char)depth; return recurse(depth + 1) + pad[0]; }
static volatile int sink;
static void job(void *argument, unsigned int index)
{
    (void)argument; (void)index;
    /* Only the extra cores fault: their stacks are outside the program's. */
    volatile char here;
    for (volatile int spin = 0; spin < 300000; spin++) {}   /* let every core take jobs */
    if ((unsigned int)&here < APP_LOAD_MIN || (unsigned int)&here >= APP_STACK_TOP) sink = divide();
}

int main(int argc, const char *const *argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    if (same(what, "div")) return divide();
    if (same(what, "gp")) { __asm__ volatile("mov %0, %%ds" :: "r"(0x1234)); return 0; }
    if (same(what, "stack")) return recurse(0);
    if (same(what, "core")) { barnix->parallel(job, 0, 64); return 0; }
    barnix->puts("usage: fault_probe div|gp|stack|core");
    return 2;
}
