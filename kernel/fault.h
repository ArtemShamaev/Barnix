#ifndef FAULT_H
#define FAULT_H
#include <stdint.h>
/* CPU exception gates for the kernel and native programs: a fault shows a
 * KERNEL PANIC with its cause instead of rebooting the machine. */
void fault_init(void);
/* While a native program runs: identity paging with a guard page below its
 * stack. Returns the previous CR0 for fault_guard_leave (nested programs). */
uint32_t fault_guard_enter(uint32_t image_end);
void fault_guard_leave(uint32_t previous_cr0);
/* Reload the gates (after a Linux program used its own table). */
void fault_load(void);
/* Fill 32 gates for another processor; nmi replaces vector 2 if given. */
void fault_fill_idt(uint64_t *table, void (*nmi)(void));
/* The boot processor reports a fault an extra core hit in a parallel job. */
void fault_check_extra_cores(void);
#endif
