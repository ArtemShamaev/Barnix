/* Extended memory for native programs and video. Everything below 32 MiB is
 * the kernel, the program arena and Linux task snapshots. Above it, GRUB
 * modules (the live ext2 image and Setup payloads) and the Multiboot tables
 * stay reserved; the largest remaining usable RAM range is handed out. */
#include "memory.h"
#include "barnix.h"

#define MEMORY_FLOOR 0x02000000U
#define MEMORY_CAP 0x80000000U
#define RESERVED_MAX 72

static unsigned int region_base, region_end, kernel_top;
static unsigned int reserved_start[RESERVED_MAX], reserved_end[RESERVED_MAX], reserved_count;
static unsigned int low_start[8], low_end[8], low_count;   /* usable RAM below 1 MiB */

static void reserve(unsigned int start, unsigned int bytes)
{
    if (!bytes || reserved_count == RESERVED_MAX) return;
    unsigned int end = bytes > 0xffffffffU - start ? 0xffffffffU : start + bytes;
    reserved_start[reserved_count] = start & ~4095U;
    reserved_end[reserved_count++] = end > 0xfffff000U ? 0xffffffffU : (end + 4095U) & ~4095U;
}
static unsigned int string_bytes(unsigned int address)
{
    if (!address) return 0;
    const char *s = (const char *)(uintptr_t)address;
    unsigned int n = 0;
    while (n < 4096 && s[n]) n++;
    return n + 1;
}
/* Largest piece of [start, end) that no reserved range touches. */
static void consider(unsigned int start, unsigned int end)
{
    if (start < MEMORY_FLOOR) start = MEMORY_FLOOR;
    while (start < end) {
        int first = -1;   /* the lowest reserved range overlapping [start, end) */
        for (unsigned int i = 0; i < reserved_count; i++)
            if (reserved_end[i] > start && reserved_start[i] < end &&
                (first < 0 || reserved_start[i] < reserved_start[first])) first = (int)i;
        unsigned int stop = first < 0 || reserved_start[first] > end ? end :
                            reserved_start[first] > start ? reserved_start[first] : start;
        if (stop - start > region_end - region_base) { region_base = start; region_end = stop; }
        if (first < 0) return;
        start = reserved_end[first];
    }
}

void memory_init(const MultibootInfo *info)
{
    if (!info) return;
    reserve((unsigned int)(uintptr_t)info, sizeof(*info));
    if (info->flags & MULTIBOOT_CMDLINE) reserve(info->command_line, string_bytes(info->command_line));
    /* Tables GRUB may leave in low memory; kept away from memory_low_free. */
    if (info->flags & (1U << 7)) reserve(info->drives_addr, info->drives_length);
    if (info->flags & (1U << 8)) reserve(info->config_table, 512);
    if (info->flags & (1U << 9)) reserve(info->boot_loader_name, string_bytes(info->boot_loader_name));
    if (info->flags & (1U << 10)) reserve(info->apm_table, 64);
    if (info->flags & (1U << 11)) { reserve(info->vbe_control_info, 512); reserve(info->vbe_mode_info, 256); }
    if ((info->flags & MULTIBOOT_MODULES) && info->module_count <= 32) {
        const MultibootModule *module = (const MultibootModule *)(uintptr_t)info->modules;
        reserve(info->modules, info->module_count * sizeof(*module));
        for (unsigned int i = 0; i < info->module_count; i++) {
            if (module[i].end > module[i].start) reserve(module[i].start, module[i].end - module[i].start);
            reserve(module[i].command_line, string_bytes(module[i].command_line));
        }
    }
    if ((info->flags & (1U << 6)) && info->mmap_addr && info->mmap_length <= 65536) {
        reserve(info->mmap_addr, info->mmap_length);
        for (unsigned int off = 0; off + 24 <= info->mmap_length;) {
            const unsigned int *record = (const unsigned int *)(uintptr_t)(info->mmap_addr + off);
            if (record[0] < 20 || record[0] > info->mmap_length - off - 4) break;
            unsigned long long base = record[1] | (unsigned long long)record[2] << 32;
            unsigned long long size = record[3] | (unsigned long long)record[4] << 32;
            if (record[5] == 1 && base < 0x100000ULL && low_count < 8) {
                unsigned long long end = base + size > 0x100000ULL ? 0x100000ULL : base + size;
                low_start[low_count] = (unsigned int)base; low_end[low_count++] = (unsigned int)end;
            }
            if (record[5] == 1 && base < MEMORY_CAP) {
                unsigned long long end = base + size > MEMORY_CAP ? MEMORY_CAP : base + size;
                consider(((unsigned int)base + 4095U) & ~4095U, (unsigned int)end & ~4095U);
            }
            off += record[0] + 4;
        }
    } else if (info->flags & 1U) {
        unsigned long long end = 0x100000ULL + (unsigned long long)info->mem_upper * 1024;
        consider(0x100000U, (unsigned int)(end > MEMORY_CAP ? MEMORY_CAP : end) & ~4095U);
    }
    kernel_top = region_base;
}

int memory_low_free(unsigned int start, unsigned int bytes)
{
    unsigned int end = start + bytes;
    for (unsigned int i = 0; i < reserved_count; i++)
        if (reserved_end[i] > start && reserved_start[i] < end) return 0;
    for (unsigned int i = 0; i < low_count; i++)
        if (low_start[i] <= start && end <= low_end[i]) return 1;
    return 0;
}

/* While a native program holds memory_app(), its block (and the nested
 * arena below it) start at app_floor: kernel blocks must stay below. */
static unsigned int app_floor, nested_base, nested_bytes;
void *memory_kernel_alloc(unsigned int bytes)
{
    bytes = (bytes + 4095U) & ~4095U;
    unsigned int end = app_floor ? app_floor : region_end;
    if (!kernel_top || kernel_top > end || bytes > end - kernel_top) return 0;
    void *block = (void *)(uintptr_t)kernel_top;
    kernel_top += bytes;
    return block;
}

/* The newest kernel block can be given back (the large Linux arenas). */
int memory_kernel_free(void *block, unsigned int bytes)
{
    bytes = (bytes + 4095U) & ~4095U;
    if (!block || (unsigned int)(uintptr_t)block + bytes != kernel_top) return -1;
    kernel_top = (unsigned int)(uintptr_t)block;
    return 0;
}

/* A kernel buffer of at least bytes (16 MiB steps of 1 MiB): the newest
 * block grows in place of itself; contents are not kept. */
int memory_kernel_fit(void **block, unsigned int *capacity, unsigned int bytes)
{
    if (*block && *capacity >= bytes) return 0;
    unsigned int want = bytes < (16U << 20) ? (16U << 20) : (bytes + 0xfffffU) & ~0xfffffU;
    if (*block && memory_kernel_free(*block, *capacity)) return -1;   /* something newer above it */
    void *fresh = memory_kernel_alloc(want);
    if (!fresh) {
        if (*block) *block = memory_kernel_alloc(*capacity);   /* the old one, at the same place */
        return -1;
    }
    *block = fresh; *capacity = want;
    return 0;
}

unsigned int memory_available(void) { return kernel_top ? region_end - kernel_top : 0; }

static unsigned int nested_reserve(unsigned int available)
{
    /* Keep a separate arena for programs run from Happy Gouse. Its memory
     * must not overlap the desktop's bitmap and editor state. */
    return available >= (256U << 20) ? (128U << 20) :
           available >= (128U << 20) ? (64U << 20) :
           available >= (64U << 20) ? (32U << 20) : 0;
}
void *memory_app_keep(unsigned int *bytes, unsigned int keep)
{
    if (!bytes) return 0;
    unsigned int available = memory_available();
    if (keep > available) keep = available;
    available -= keep;
    unsigned int reserved = nested_reserve(available);
    available -= reserved;
    if (*bytes > 0xfffff000U) *bytes = 0xfffff000U;
    if (*bytes) *bytes = (*bytes + 4095U) & ~4095U;
    if (!*bytes || *bytes > available) *bytes = available;
    if (!*bytes) return 0;
    app_floor = nested_base = kernel_top + keep;
    nested_bytes = reserved;
    memset((void *)(uintptr_t)(app_floor + reserved), 0, *bytes);
    return (void *)(uintptr_t)(app_floor + reserved);
}
void *memory_app(unsigned int *bytes) { return memory_app_keep(bytes, 0); }
void memory_app_release(void) { app_floor = nested_base = nested_bytes = 0; }

void *memory_nested(unsigned int *bytes)
{
    unsigned int base = nested_base ? nested_base : kernel_top;
    unsigned int reserved = nested_base ? nested_bytes : nested_reserve(memory_available());
    if (!bytes || !reserved) return 0;
    if (*bytes > reserved || !*bytes) *bytes = reserved;
    memset((void *)(uintptr_t)base, 0, *bytes);
    return (void *)(uintptr_t)base;
}
