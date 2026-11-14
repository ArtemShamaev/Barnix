#include "linux_memory.h"
#include "app_abi.h"
#include "barnix.h"
#include "memory.h"
#include "lang.h"
#include "linux_limits.h"
#include <stdint.h>

/* Keep low kernel mappings supervisor-only. Map the reserved physical arena
 * into the validated ELF virtual window, independently of its link address. */
static uint32_t directory[1024] __attribute__((aligned(4096)));
/* Match the physical RAM ceiling used by memory.c: filesystem buffers and
 * live modules above 32 MiB must remain accessible inside system calls. */
#define KERNEL_RAM_TABLES (0x80000000U >> 22)
static uint32_t tables[KERNEL_RAM_TABLES][1024] __attribute__((aligned(4096)));
static uint32_t user_table[1024] __attribute__((aligned(4096)));
static uint32_t large_user_tables[LINUX_LARGE_ARENA_SIZE >> 22][1024]
    __attribute__((aligned(4096)));
static void *large_user_arena;
static int large_kept;   /* reserved for Happy Gouse: stays between programs */
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
            /* The table replaces the whole 4 MiB: keep its other pages as
             * before (another device's registers, RAM) and not absent. */
            for (unsigned int j = 0; j < 1024; j++) {
                unsigned int address = (index * 1024 + j) * 4096U;
                mmio_tables[slot][j] = index < KERNEL_RAM_TABLES && address ? address | 3U : 0;
            }
        }
        /* Supervisor-only, uncached device registers. */
        mmio_tables[slot][page & 1023] = (page << 12) | 0x1b;
    }
    return 0;
}
static unsigned int user_base, user_top;
static unsigned int heap_base, heap_break;
static unsigned char read_only[LINUX_LARGE_BITMAP_SIZE];
static unsigned char mmap_pages[LINUX_LARGE_BITMAP_SIZE];
static int mmap_used(unsigned int page)
{ return (mmap_pages[page >> 3] >> (page & 7)) & 1; }
static void mmap_set(unsigned int page, int used)
{
    unsigned char mask = 1U << (page & 7);
    if (used) mmap_pages[page >> 3] |= mask;
    else mmap_pages[page >> 3] &= ~mask;
}
static uint32_t *user_page_table(unsigned int page)
{
    return user_base == LINUX_LARGE_BASE ? &large_user_tables[page >> 10][page & 1023] :
           &user_table[page];
}
unsigned int linux_memory_current_base(void) { return user_base; }
int linux_memory_prepare(unsigned int base)
{
    if (base != LINUX_LARGE_BASE) return 0;
    for (unsigned int i = 0; i < mmio_count; i++)
        if (mmio_indices[i] >= (LINUX_LARGE_BASE >> 22) &&
            mmio_indices[i] < ((LINUX_LARGE_BASE + LINUX_LARGE_ARENA_SIZE) >> 22)) {
            /* device registers where the program would be mapped */
            println(RED, tr("device memory occupies 0x80000000-0x8fffffff: large programs cannot run"));
            return -12;
        }
    if (!large_user_arena) large_user_arena = memory_kernel_alloc(LINUX_LARGE_ARENA_SIZE);
    return large_user_arena ? 0 : -12;
}
void linux_memory_keep(int keep) { large_kept = keep; }
/* A program run from the shell takes the arena only while it runs: native
 * programs started later (BJVM) need that memory. */
void linux_memory_release(void)
{
    if (large_user_arena && !large_kept && !memory_kernel_free(large_user_arena, LINUX_LARGE_ARENA_SIZE))
        large_user_arena = 0;
}
void *linux_memory_physical_base(unsigned int base)
{ return base == LINUX_LARGE_BASE ? large_user_arena : (void *)APP_LOAD_MIN; }
unsigned int linux_memory_arena_size(unsigned int base)
{ return base == LINUX_LARGE_BASE ? LINUX_LARGE_ARENA_SIZE : APP_STACK_TOP - APP_LOAD_MIN; }
void linux_memory_enter(unsigned int base, unsigned int image_end)
{
    unsigned int arena_size = linux_memory_arena_size(base);
    unsigned int physical = (unsigned int)linux_memory_physical_base(base);
    user_base = base; user_top = base + arena_size;
    heap_base = heap_break = (image_end + 4095U) & ~4095U;
    memset(read_only, 0, sizeof(read_only));
    memset(mmap_pages, 0, sizeof(mmap_pages));
    memset(directory, 0, sizeof(directory));
    for (unsigned int i = 0; i < KERNEL_RAM_TABLES; i++) {
        directory[i] = (uint32_t)tables[i] | 7U;
        for (unsigned int j = 0; j < 1024; j++) {
            uint32_t address = (i * 1024 + j) * 4096U;
            uint32_t flags = 3U;
            tables[i][j] = address ? address | flags : 0;
        }
    }
    if (base == LINUX_LARGE_BASE) {
        for (unsigned int page = 0; page < arena_size / 4096U; page++)
            large_user_tables[page >> 10][page & 1023] = (physical + page * 4096U) | 7U;
        for (unsigned int i = 0; i < arena_size / 0x400000U; i++)
            directory[(base >> 22) + i] = (uint32_t)large_user_tables[i] | 7U;
    } else {
        for (unsigned int i = 0; i < 1024; i++)
            user_table[i] = (APP_LOAD_MIN + i * 4096U) | 7U;
        directory[base >> 22] = (uint32_t)user_table | 7U;
    }
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
    unsigned int stack_size = user_base == LINUX_LARGE_BASE ? LINUX_LARGE_STACK_SIZE : APP_STACK_SIZE;
    unsigned int max_break = user_top - stack_size;
    unsigned int pages = (max_break - user_base) >> 12;
    for (unsigned int page = 0; page < pages; page++)
        if (mmap_used(page) && user_base + page * 4096U < max_break)
            max_break = user_base + page * 4096U;
    if (requested >= heap_base && requested <= max_break) {
        if (requested > heap_break) memset((void *)heap_break, 0, requested - heap_break);
        heap_break = requested;
    }
    return heap_break;
}

int linux_memory_map(unsigned int size, unsigned int protection)
{
    if (!size || size > 0x7ffffffU || (protection & ~7U) || !protection)
        return -22;
    unsigned int count = (size + 4095U) >> 12;
    unsigned int heap_page = (heap_break - user_base + 4095U) >> 12;
    unsigned int top_page = (user_top - user_base -
        (user_base == LINUX_LARGE_BASE ? LINUX_LARGE_STACK_SIZE : APP_STACK_SIZE)) >> 12;
    for (unsigned int end = top_page; end >= heap_page + count; end--) {
        unsigned int first = end - count;
        unsigned int page = first;
        for (; page < end; page++) if (mmap_used(page)) break;
        if (page != end) continue;
        unsigned int result = user_base + first * 4096U;
        memset((void *)result, 0, count * 4096U);
        for (page = first; page < end; page++) mmap_set(page, 1);
        if (!(protection & 2U)) linux_memory_protect(result, count * 4096U, protection | 1U);
        return (int)result;
    }
    return -12; /* ENOMEM */
}

int linux_memory_unmap(unsigned int address, unsigned int size)
{
    if ((address & 4095U) || !size || !linux_user_range(address, size)) return -22;
    unsigned int first = (address - user_base) >> 12;
    unsigned int count = (size + 4095U) >> 12;
    for (unsigned int page = first; page < first + count; page++) {
        if (!mmap_used(page)) return -22;
    }
    linux_memory_protect(address, count * 4096U, 3);
    memset((void *)address, 0, count * 4096U);
    for (unsigned int page = first; page < first + count; page++) mmap_set(page, 0);
    return 0;
}

int linux_memory_protect(unsigned int address, unsigned int size, unsigned int protection)
{
    if (address & 4095U || !size || (protection & ~7U) || !protection ||
        !linux_user_range(address, size)) return -22;
    unsigned int first = (address - user_base) >> 12;
    unsigned int last = (address - user_base + size - 1) >> 12;
    for (unsigned int page = first; page <= last; page++) {
        unsigned int mask = 1U << (page & 7);
        if (protection & 2U) read_only[page >> 3] &= ~mask;
        else read_only[page >> 3] |= mask;
        uint32_t *pte = user_page_table(page);
        if (protection & 2U) *pte |= 2U;
        else *pte &= ~2U;
        unsigned int virtual = user_base + page * 4096U;
        __asm__ volatile("invlpg (%0)" : : "r"(virtual) : "memory");
    }
    return 0;
}

int linux_user_range(unsigned int address, unsigned int size)
{
    return address >= user_base && address <= user_top && size <= user_top - address;
}

void linux_memory_capture(LinuxMemoryState *state)
{
    state->base = user_base; state->heap_base = heap_base; state->heap_break = heap_break;
    memcpy(state->read_only, read_only, sizeof(read_only));
    memcpy(state->mmap_pages, mmap_pages, sizeof(mmap_pages));
}
void linux_memory_resume(const LinuxMemoryState *state)
{
    linux_memory_enter(state->base, state->heap_base);
    heap_break = state->heap_break;
    memcpy(read_only, state->read_only, sizeof(read_only));
    memcpy(mmap_pages, state->mmap_pages, sizeof(mmap_pages));
    unsigned int pages = linux_memory_arena_size(state->base) >> 12;
    for (unsigned int page = 0; page < pages; page++)
        if (read_only[page >> 3] & (1U << (page & 7))) *user_page_table(page) &= ~2U;
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)directory) : "memory");
}
