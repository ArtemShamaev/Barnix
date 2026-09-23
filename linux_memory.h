#ifndef BARNIX_LINUX_MEMORY_H
#define BARNIX_LINUX_MEMORY_H
typedef struct { unsigned int base, heap_base, heap_break; } LinuxMemoryState;
void linux_memory_capture(LinuxMemoryState *state);
void linux_memory_resume(const LinuxMemoryState *state);
void linux_memory_enter(unsigned int base, unsigned int image_end);
int linux_user_range(unsigned int address, unsigned int size);
void linux_memory_leave(void);
unsigned int linux_memory_brk(unsigned int requested);
#endif
