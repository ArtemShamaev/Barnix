#include "linux_memory.h"
#include "app_abi.h"
#include "barnix.h"
#include <stdint.h>

/* Keep low kernel mappings supervisor-only. Map the reserved physical arena
 * into the validated ELF virtual window, independently of its link address. */
static uint32_t directory[1024] __attribute__((aligned(4096)));
static uint32_t tables[8][1024] __attribute__((aligned(4096)));
static uint32_t user_table[1024] __attribute__((aligned(4096)));
static uint32_t saved_cr0, saved_cr3;
static uint32_t mmio_tables[32][1024] __attribute__((aligned(4096)));
static unsigned int mmio_indices[32], mmio_count;
int linux_memory_map_mmio(unsigned int base, unsigned int size)
{
    if (!size || base < 0x10000000U || size - 1 > 0xffffffffU - base) return -1;
    unsigned int last = (base + size - 1) >> 12;
    for (unsigned int page = base >> 12; page <= last; page++) {
        unsigned int index = page >> 10, slot = 0;
        while (slot < mmio_count && mmio_indices[slot] != index) slot++;
        if (slot == mmio_count) {
            if (mmio_count == 32) return -1;
            mmio_indices[mmio_count++] = index;
        }
        /* Supervisor-only, uncached device registers. */
        mmio_tables[slot][page & 1023] = (page << 12) | 0x1b;
    }
    return 0;
}
static unsigned int user_base, user_top;
static unsigned int heap_base, heap_break;
void linux_memory_enter(unsigned int base, unsigned int image_end)
{
    user_base = base; user_top = base + APP_STACK_TOP - APP_LOAD_MIN;
    heap_base = heap_break = (image_end + 4095U) & ~4095U;
    memset(directory, 0, sizeof(directory));
    for (unsigned int i = 0; i < 8; i++) {
        directory[i] = (uint32_t)tables[i] | 7U;
        for (unsigned int j = 0; j < 1024; j++) {
            uint32_t address = (i * 1024 + j) * 4096U;
            uint32_t flags = 3U;
            tables[i][j] = address ? address | flags : 0;
        }
    }
    for (unsigned int i = 0; i < 1024; i++)
        user_table[i] = (APP_LOAD_MIN + i * 4096U) | 7U;
    directory[base >> 22] = (uint32_t)user_table | 7U;
    for (unsigned int i = 0; i < mmio_count; i++)
        directory[mmio_indices[i]] = (uint32_t)mmio_tables[i] | 3U;
    __asm__ volatile("mov %%cr0, %0; mov %%cr3, %1" : "=r"(saved_cr0), "=r"(saved_cr3));
    __asm__ volatile("mov %0, %%cr3; mov %1, %%cr0" : :
        "r"((uint32_t)directory), "r"(saved_cr0 | 0x80000000U) : "memory");
}
void linux_memory_leave(void)
{
    __asm__ volatile("mov %0, %%cr0; mov %1, %%cr3" : :
        "r"(saved_cr0), "r"(saved_cr3) : "memory");
}
unsigned int linux_memory_brk(unsigned int requested)
{
    if (requested >= heap_base && requested <= user_top - APP_STACK_SIZE) {
        if (requested > heap_break) memset((void *)heap_break, 0, requested - heap_break);
        heap_break = requested;
    }
    return heap_break;
}

int linux_user_range(unsigned int address, unsigned int size)
{
    return address >= user_base && address <= user_top && size <= user_top - address;
}

void linux_memory_capture(LinuxMemoryState *state)
{
    state->base = user_base; state->heap_base = heap_base; state->heap_break = heap_break;
}
void linux_memory_resume(const LinuxMemoryState *state)
{
    linux_memory_enter(state->base, state->heap_base);
    heap_break = state->heap_break;
}
