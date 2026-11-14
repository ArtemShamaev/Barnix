#ifndef BARNIX_LINUX_MEMORY_H
#define BARNIX_LINUX_MEMORY_H
#include "linux_limits.h"
int linux_memory_map_mmio(unsigned int base, unsigned int size);
int linux_memory_prepare(unsigned int base);
void *linux_memory_physical_base(unsigned int base);
unsigned int linux_memory_arena_size(unsigned int base);
void linux_memory_keep(int keep);
void linux_memory_release(void);
typedef struct {
    unsigned int base, heap_base, heap_break;
    unsigned char read_only[LINUX_LARGE_BITMAP_SIZE];
    unsigned char mmap_pages[LINUX_LARGE_BITMAP_SIZE];
} LinuxMemoryState;
void linux_memory_capture(LinuxMemoryState *state);
void linux_memory_resume(const LinuxMemoryState *state);
void linux_memory_enter(unsigned int base, unsigned int image_end);
int linux_user_range(unsigned int address, unsigned int size);
void linux_memory_leave(void);
unsigned int linux_memory_current_base(void);   /* the running process's window */
unsigned int linux_memory_brk(unsigned int requested);
int linux_memory_protect(unsigned int address, unsigned int size, unsigned int protection);
int linux_memory_map(unsigned int size, unsigned int protection);
int linux_memory_unmap(unsigned int address, unsigned int size);
#endif
