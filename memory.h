#ifndef MEMORY_H
#define MEMORY_H
#include "multiboot.h"
/* Free RAM above the kernel arenas, GRUB modules and Multiboot tables. */
void memory_init(const MultibootInfo *info);
/* Permanent kernel blocks (video shadow), taken before programs run. */
void *memory_kernel_alloc(unsigned int bytes);
unsigned int memory_available(void);
int memory_kernel_free(void *block, unsigned int bytes);
int memory_kernel_fit(void **block, unsigned int *capacity, unsigned int bytes);
/* 1 when [start, start + bytes) below 1 MiB is usable RAM that holds no
 * Multiboot data (for the processor start-up code). */
int memory_low_free(unsigned int start, unsigned int bytes);
/* The rest, zeroed, for the running native program: *bytes in = wanted
 * (0 = all), out = granted. Valid until the program exits. */
void *memory_app(unsigned int *bytes);
/* The same, leaving keep bytes for later kernel blocks (the Linux compiler
 * arenas); memory_kernel_alloc never reaches the program's block. */
void *memory_app_keep(unsigned int *bytes, unsigned int keep);
/* The program exited: its block is free again. */
void memory_app_release(void);
/* Separate arena for a program invoked through ABI 13 run(). */
void *memory_nested(unsigned int *bytes);
#endif
