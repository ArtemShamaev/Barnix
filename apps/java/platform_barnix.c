/* Barnix platform for BJVM: the program's extended memory is split into the
 * C allocator arena, the Java heap and the OpenGL memory; files, time,
 * console and sockets go through the Barnix application API. */
#include "jvm.h"
#include <barnix_api.h>
#include <barnix_gfx.h>

/* ---- memory ---- */
static uint8_t *region, *region_end, *region_free;
static void region_init(void)
{
    if (region) return;
    unsigned int bytes = 0;
    region = barnix->memory ? barnix->memory(&bytes) : 0;
    if (!region) { region = region_end = region_free = (uint8_t *)16; return; }
    region_end = region + bytes;
    region_free = region;
}
static void *region_take(uint32_t *bytes)
{
    region_init();
    uint32_t left = (uint32_t)(region_end - region_free);
    if (*bytes > left) *bytes = left & ~15U;
    if (!*bytes) return 0;
    void *p = region_free;
    region_free += (*bytes + 15) & ~15U;
    return p;
}
static uint32_t region_left(void) { region_init(); return (uint32_t)(region_end - region_free); }

/* The C allocator: first fit over a free list with
 * boundary tags; neighbours merge on free. */
typedef struct Chunk { uint32_t size, used; struct Chunk *next, *prev; } Chunk;   /* size includes the header */
#define CHUNK_MIN 32U
static Chunk *free_list;
static uint8_t *arena, *arena_end;
static uint32_t *footer(Chunk *c) { return (uint32_t *)((uint8_t *)c + c->size) - 1; }
static void unlink_chunk(Chunk *c)
{
    if (c->prev) c->prev->next = c->next; else free_list = c->next;
    if (c->next) c->next->prev = c->prev;
}
static void insert_chunk(Chunk *c)
{
    c->used = 0;
    *footer(c) = c->size;
    c->prev = 0; c->next = free_list;
    if (free_list) free_list->prev = c;
    free_list = c;
}
static void arena_init(void)
{
    region_init();
    /* A quarter of the memory, between 24 and 64 MiB. */
    uint32_t bytes = region_left() / 4;
    if (bytes < (24U << 20)) bytes = 24U << 20;
    if (bytes > (64U << 20)) bytes = 64U << 20;
    arena = region_take(&bytes);
    if (!arena || bytes < 4096) { arena = arena_end = 0; return; }
    arena_end = arena + (bytes & ~15U);
    Chunk *c = (Chunk *)arena;
    c->size = (uint32_t)(arena_end - arena);
    insert_chunk(c);
}
void *vm_malloc(uint32_t bytes)
{
    if (!arena) arena_init();
    uint32_t need = (bytes + 16 + 4 + 15) & ~15U;   /* header, footer */
    if (need < CHUNK_MIN) need = CHUNK_MIN;
    for (Chunk *c = free_list; c; c = c->next) {
        if (c->size < need) continue;
        unlink_chunk(c);
        if (c->size - need >= CHUNK_MIN) {
            Chunk *rest = (Chunk *)((uint8_t *)c + need);
            rest->size = c->size - need;
            insert_chunk(rest);
            c->size = need;
        }
        c->used = 1;
        *footer(c) = c->size;
        memset((uint8_t *)c + 16, 0, c->size - 20);
        return (uint8_t *)c + 16;
    }
    return 0;
}
void vm_free(void *p)
{
    if (!p) return;
    Chunk *c = (Chunk *)((uint8_t *)p - 16);
    uint8_t *after = (uint8_t *)c + c->size;
    if (after < arena_end && !((Chunk *)after)->used) { unlink_chunk((Chunk *)after); c->size += ((Chunk *)after)->size; }
    if ((uint8_t *)c > arena) {
        Chunk *before = (Chunk *)((uint8_t *)c - ((uint32_t *)c)[-1]);
        if (!before->used) { unlink_chunk(before); before->size += c->size; c = before; }
    }
    insert_chunk(c);
}

/* Java heap: -Xmx, else 40% of what the allocator leaves; OpenGL gets the rest. */
void *plat_heap(uint32_t *bytes)
{
    if (!arena) arena_init();
    if (!*bytes) *bytes = region_left() / 5 * 2;
    if (*bytes > region_left() - (16U << 20)) *bytes = region_left() > (32U << 20) ? region_left() - (16U << 20) : region_left() / 2;
    return region_take(bytes);
}
void *plat_gl_memory(uint32_t *bytes)
{
    static void *memory;
    static uint32_t size;
    if (!memory) { size = region_left(); memory = region_take(&size); }
    *bytes = size;
    return memory;
}

/* ---- time ---- */
static uint64_t tsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }
static uint64_t tsc_start, tsc_per_ms;
static void clock_init(void)
{
    if (tsc_per_ms) return;
    uint64_t a = tsc();
    barnix->sleep_ms(50);
    tsc_per_ms = (tsc() - a) / 50;
    if (!tsc_per_ms) tsc_per_ms = 1;
    tsc_start = tsc();
}
uint64_t plat_millis(void) { clock_init(); return (tsc() - tsc_start) / tsc_per_ms; }
uint64_t plat_nanos(void)
{
    clock_init();
    uint64_t t = tsc() - tsc_start, ms = t / tsc_per_ms;
    return ms * 1000000ULL + (t - ms * tsc_per_ms) * 1000000ULL / tsc_per_ms;
}
/* Barnix has no wall clock for programs: time starts at 2026-11-03 00:00 UTC. */
jlong plat_epoch_ms(void) { return 1793664000000LL + (jlong)plat_millis(); }
void plat_idle(uint32_t ms) { barnix->sleep_ms(ms ? ms : 1); }

/* ---- console ---- */
void plat_write(int err, const char *text, uint32_t length)
{
    char chunk[257];
    while (length) {
        uint32_t n = length > 256 ? 256 : length;
        memcpy(chunk, text, n);
        chunk[n] = 0;
        barnix->print(err ? 12 : 15, chunk);
        text += n; length -= n;
    }
}
int plat_poll_key(void)
{
    unsigned int k = barnix->getch();
    return k == '\r' ? '\n' : (int)(k & 0xff);
}

/* ---- files ---- */
int plat_file_size(const char *path) { return barnix->size(path); }
int plat_file_read(const char *path, uint32_t offset, void *data, uint32_t size) { return barnix->read(path, offset, data, size); }
int plat_file_write(const char *path, const void *data, uint32_t size, int append)
{
    int r = append && barnix->size(path) >= 0 ? barnix->append(path, data, (int)size) : barnix->write(path, data, (int)size);
    return r < 0 ? -1 : 0;
}
int plat_is_dir(const char *path)
{
    BarnixDirEntry e;
    uint32_t position = 0;
    return barnix->size(path) < 0 && barnix->directory(path, &position, &e) >= 0;
}
int plat_list(const char *path, uint32_t *position, char *name, uint32_t capacity)
{
    BarnixDirEntry e;
    int r = barnix->directory(path, position, &e);
    if (r <= 0) return r;
    uint32_t n = 0;
    while (e.name[n] && n + 1 < capacity) { name[n] = e.name[n]; n++; }
    name[n] = 0;
    return 1;
}
int plat_mkdir(const char *path) { return barnix->mkdir(path) < 0 ? -1 : 0; }
int plat_delete(const char *path) { return (plat_is_dir(path) ? barnix->rmdir(path) : barnix->rm(path)) < 0 ? -1 : 0; }
int plat_rename(const char *from, const char *to) { return barnix->mv(from, to) < 0 ? -1 : 0; }
int plat_getcwd(char *out, uint32_t capacity) { return barnix->getcwd(out, capacity) < 0 ? -1 : 0; }
const char *plat_user(void) { const char *u = barnix->current_user(); return u && *u ? u : "user"; }
const char *plat_os(void) { return "Barnix"; }
void plat_random(void *out, uint32_t length) { barnix->random(out, length); }
uint32_t plat_cpus(void) { return barnix->cpus ? barnix->cpus() : 1; }

/* ---- sockets ---- */
int plat_resolve(const char *host, uint32_t *address) { return barnix->net_resolve(host, address) < 0 ? -1 : 0; }
int plat_net_open(const char *host, uint32_t port) { return barnix->net_open(host, port); }
int plat_net_send(int h, const void *d, uint32_t n) { return barnix->net_send(h, d, n); }
int plat_net_recv(int h, void *d, uint32_t n, uint32_t ms) { return barnix->net_recv(h, d, n, ms); }
void plat_net_close(int h) { barnix->net_close(h); }
