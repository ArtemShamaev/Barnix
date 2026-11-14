#ifndef SMP_H
#define SMP_H
/* Extra processor cores for native programs. The kernel itself stays on the
 * boot processor; the others only run smp_parallel jobs, with paging off and
 * interrupts disabled, and sleep in hlt until an NMI wakes them. */
void smp_init(void);
/* Processors that take part in smp_parallel, the boot processor included. */
unsigned int smp_cpus(void);
/* Calls job(argument, i) once for every i in 0..count-1 on all processors
 * and returns when every call has finished. Jobs must not use the Barnix API. */
void smp_parallel(void (*job)(void *argument, unsigned int index), void *argument, unsigned int count);
#endif
