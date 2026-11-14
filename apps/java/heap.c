/* The Java heap: 4 KiB pages. Objects up to 2 KiB live in size-class blocks
 * of 1-4 pages; larger ones take page runs. Mark-sweep, non-moving: roots
 * are class statics, interned strings, pinned C globals and every thread's
 * Java slots and C stack, scanned conservatively against the bitmap of
 * object starts (one bit per 8 bytes). */
#include "jvm.h"

#define PAGE 4096U
#define GRAIN 8U
enum { P_FREE, P_SMALL, P_SMALL_TAIL, P_LARGE, P_LARGE_TAIL };
enum { NONE = 0xffffffffU };
typedef struct {
    uint8_t kind, cls;
    uint16_t live;
    uint32_t count;              /* free/large run length; small: block start for tails */
    uint32_t next;               /* next free run / next block of a class */
    void *free;                  /* small block free list */
} Page;

static const uint16_t sizes[] = {16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256,
                                 320, 384, 448, 512, 640, 768, 1024, 1280, 1536, 2048};
#define CLASSES (sizeof(sizes) / sizeof(sizes[0]))
static uint8_t class_of_size[2048 / GRAIN + 1];
static uint32_t class_blocks[CLASSES];   /* blocks that may have free slots */
static uint8_t *heap_base, *heap_end;
static uint32_t page_count, free_runs, free_pages;
static Page *pages;
static uint32_t *alloc_bits, *mark_bits;
static uint32_t used_bytes, since_gc, gc_threshold;
static Object **pins[256];
static unsigned pin_count;
static int collecting;

static inline uint32_t grain(const void *p) { return (uint32_t)((const uint8_t *)p - heap_base) / GRAIN; }
static inline int bit(const uint32_t *map, uint32_t i) { return map[i >> 5] >> (i & 31) & 1; }
static inline void set_bit(uint32_t *map, uint32_t i) { map[i >> 5] |= 1U << (i & 31); }
static inline void clear_bit(uint32_t *map, uint32_t i) { map[i >> 5] &= ~(1U << (i & 31)); }
static uint32_t block_pages(unsigned c) { return sizes[c] <= 256 ? 1 : sizes[c] <= 1024 ? 2 : 4; }

void heap_init(void *base, uint32_t bytes)
{
    uint8_t *start = (uint8_t *)(((uintptr_t)base + PAGE - 1) & ~(uintptr_t)(PAGE - 1));
    bytes -= (uint32_t)(start - (uint8_t *)base);
    page_count = bytes / PAGE;
    heap_base = start;
    heap_end = start + page_count * PAGE;
    pages = meta_alloc(page_count * sizeof(Page));
    uint32_t words = (page_count * (PAGE / GRAIN) + 31) / 32;
    alloc_bits = meta_alloc(words * 4);
    mark_bits = meta_alloc(words * 4);
    for (unsigned c = 0, s = 0; s <= 2048 / GRAIN; s++) {
        while (sizes[c] < s * GRAIN) c++;
        class_of_size[s] = (uint8_t)c;
    }
    for (unsigned c = 0; c < CLASSES; c++) class_blocks[c] = NONE;
    pages[0].kind = P_FREE; pages[0].count = page_count; pages[0].next = NONE;
    free_runs = 0; free_pages = page_count;
    gc_threshold = page_count * PAGE / 3;
}

uint32_t heap_total_bytes(void) { return page_count * PAGE; }
uint32_t heap_free_bytes(void) { return page_count * PAGE - used_bytes; }
void heap_pin(Object **root) { if (pin_count < 256) pins[pin_count++] = root; }

/* First fit over the free runs, which are kept in address order. */
static uint32_t take_pages(uint32_t n)
{
    uint32_t *link = &free_runs;
    for (uint32_t r = free_runs; r != NONE; link = &pages[r].next, r = pages[r].next) {
        if (pages[r].count < n) continue;
        uint32_t rest = pages[r].count - n;
        if (rest) {
            uint32_t s = r + n;
            pages[s].kind = P_FREE; pages[s].count = rest; pages[s].next = pages[r].next;
            *link = s;
        } else *link = pages[r].next;
        free_pages -= n;
        return r;
    }
    return NONE;
}

static void *alloc_small(unsigned c)
{
    for (;;) {
        uint32_t b = class_blocks[c];
        if (b == NONE) break;
        Page *p = &pages[b];
        if (p->free) {
            void *slot = p->free;
            p->free = *(void **)slot;
            p->live++;
            return slot;
        }
        class_blocks[c] = p->next;
    }
    uint32_t n = block_pages(c), b = take_pages(n);
    if (b == NONE) return 0;
    Page *p = &pages[b];
    p->kind = P_SMALL; p->cls = (uint8_t)c; p->live = 0; p->count = n;
    for (uint32_t i = 1; i < n; i++) { pages[b + i].kind = P_SMALL_TAIL; pages[b + i].count = b; }
    uint8_t *start = heap_base + b * PAGE;
    uint32_t size = sizes[c], slots = n * PAGE / size;
    void *list = 0;
    for (uint32_t i = slots; i-- > 0;) { *(void **)(start + i * size) = list; list = start + i * size; }
    p->free = *(void **)list;
    p->live = 1;
    p->next = class_blocks[c];
    class_blocks[c] = b;
    return list;
}

static void *alloc_large(uint32_t bytes)
{
    uint32_t n = (bytes + PAGE - 1) / PAGE, b = take_pages(n);
    if (b == NONE) return 0;
    pages[b].kind = P_LARGE; pages[b].count = n;
    for (uint32_t i = 1; i < n; i++) { pages[b + i].kind = P_LARGE_TAIL; pages[b + i].count = b; }
    return heap_base + b * PAGE;
}

static uint32_t object_size(const void *p)
{
    uint32_t page = (uint32_t)((const uint8_t *)p - heap_base) / PAGE;
    if (pages[page].kind == P_SMALL_TAIL) page = pages[page].count;
    if (pages[page].kind == P_SMALL) return sizes[pages[page].cls];
    return pages[page].count * PAGE;
}

static void *try_alloc(uint32_t bytes)
{
    bytes = (bytes + GRAIN - 1) & ~(GRAIN - 1);
    void *p = bytes <= 2048 ? alloc_small(class_of_size[bytes / GRAIN]) : alloc_large(bytes);
    if (!p) return 0;
    uint32_t size = object_size(p);
    memset(p, 0, bytes <= 2048 ? size : bytes);
    set_bit(alloc_bits, grain(p));
    used_bytes += size;
    since_gc += size;
    return p;
}

Object *heap_alloc(Thread *t, uint32_t bytes)
{
    if (since_gc > gc_threshold && !collecting) heap_collect(t);
    void *p = try_alloc(bytes);
    if (!p && !collecting) { heap_collect(t); p = try_alloc(bytes); }
    return p;
}

int heap_is_object(const void *p)
{
    const uint8_t *b = p;
    if (b < heap_base || b >= heap_end || ((uintptr_t)b & (GRAIN - 1))) return 0;
    return bit(alloc_bits, grain(b));
}

/* ---- marking ---- */
static Object **mark_stack;
static uint32_t mark_top, mark_capacity;
static int mark_overflow;

static inline void mark(Object *o)
{
    if (!o || !heap_is_object(o)) return;
    uint32_t g = grain(o);
    if (bit(mark_bits, g)) return;
    set_bit(mark_bits, g);
    if (mark_top < mark_capacity) mark_stack[mark_top++] = o;
    else mark_overflow = 1;
}

static void scan_object(Object *o)
{
    Class *c = o->cls;
    if (!c) return;
    if (c->flags & CF_ARRAY) {
        if (c->flags & CF_REFERENCE_ARRAY) {
            Array *a = (Array *)o;
            Object **e = ADATA(a, Object *);
            for (jint i = 0; i < a->length; i++) mark(e[i]);
        }
        return;
    }
    for (uint32_t i = 0; i < c->ref_count; i++) mark(FIELD(o, c->refs[i], Object *));
    if (c == vm.class_) {
        Class *k = mirror_class(o);
        if (k && k->mirror) mark(k->mirror);
    }
}

static void drain(void)
{
    for (;;) {
        while (mark_top) scan_object(mark_stack[--mark_top]);
        if (!mark_overflow) return;
        /* The stack overflowed: rescan marked objects to find unmarked children. */
        mark_overflow = 0;
        for (uint32_t w = 0; w < page_count * (PAGE / GRAIN) / 32; w++) {
            uint32_t m = mark_bits[w];
            while (m) {
                int b = __builtin_ctz(m);
                m &= m - 1;
                scan_object((Object *)(heap_base + (w * 32 + b) * GRAIN));
                while (mark_top) scan_object(mark_stack[--mark_top]);
            }
        }
    }
}

static void mark_range(const void *start, const void *end)
{
    uintptr_t s = ((uintptr_t)start + 3) & ~(uintptr_t)3;
    for (const uint32_t *p = (const uint32_t *)s; (const void *)(p + 1) <= end; p++)
        mark((Object *)(uintptr_t)*p);
}

static void mark_class(Class *c, void *arg)
{
    (void)arg;
    if (c->mirror) mark(c->mirror);
    if (c->statics)
        for (uint32_t i = 0; i < c->static_ref_count; i++) mark(*(Object **)(c->statics + c->static_refs[i]));
    if (c->cp.resolved)
        for (uint32_t i = 1; i < c->cp.count; i++)
            if (c->cp.tags[i] == CP_STRING && c->cp.resolved[i]) mark(c->cp.resolved[i]);
}

extern void strings_scan_roots(void (*mark_one)(Object *o));

static void sweep(void)
{
    for (unsigned c = 0; c < CLASSES; c++) class_blocks[c] = NONE;
    used_bytes = 0;
    for (uint32_t pg = 0; pg < page_count;) {
        Page *p = &pages[pg];
        if (p->kind == P_SMALL) {
            uint32_t size = sizes[p->cls], n = p->count, slots = n * PAGE / size, live = 0;
            uint8_t *start = heap_base + pg * PAGE;
            void *list = 0;
            for (uint32_t i = slots; i-- > 0;) {
                uint8_t *slot = start + i * size;
                uint32_t g = grain(slot);
                if (bit(alloc_bits, g)) {
                    if (bit(mark_bits, g)) { live++; continue; }
                    Object *o = (Object *)slot;
                    if (o->monitor) monitor_release_object(o);
                    clear_bit(alloc_bits, g);
                }
                *(void **)slot = list;
                list = slot;
            }
            if (!live) {
                for (uint32_t i = 0; i < n; i++) pages[pg + i].kind = P_FREE;
            } else {
                p->live = (uint16_t)live;
                p->free = list;
                used_bytes += live * size;
                if (list) { p->next = class_blocks[p->cls]; class_blocks[p->cls] = pg; }
            }
            pg += n;
        } else if (p->kind == P_LARGE) {
            uint32_t n = p->count, g = grain(heap_base + pg * PAGE);
            if (!bit(mark_bits, g)) {
                Object *o = (Object *)(heap_base + pg * PAGE);
                if (o->monitor) monitor_release_object(o);
                clear_bit(alloc_bits, g);
                for (uint32_t i = 0; i < n; i++) pages[pg + i].kind = P_FREE;
            } else used_bytes += n * PAGE;
            pg += n;
        } else if (p->kind == P_FREE) {
            pg++;
        } else pg++;
    }
    /* Rebuild the free runs in address order. */
    uint32_t *link = &free_runs;
    free_pages = 0;
    for (uint32_t pg = 0; pg < page_count;) {
        if (pages[pg].kind != P_FREE) { pg += pages[pg].kind == P_SMALL || pages[pg].kind == P_LARGE ? pages[pg].count : 1; continue; }
        uint32_t s = pg;
        while (pg < page_count && pages[pg].kind == P_FREE) pg++;
        pages[s].count = pg - s;
        *link = s;
        link = &pages[s].next;
        free_pages += pg - s;
    }
    *link = NONE;
}

static void mark_one(Object *o) { mark(o); }

void heap_collect(Thread *t)
{
    (void)t;
    if (collecting) return;
    collecting = 1;
    if (!mark_stack) {
        mark_capacity = 1 << 16;
        mark_stack = vm_malloc(mark_capacity * sizeof(Object *));
        if (!mark_stack) fatal("out of memory", "GC mark stack");
    }
    uint64_t start = vm_verbose ? plat_millis() : 0;
    uint32_t before = used_bytes;
    memset(mark_bits, 0, (page_count * (PAGE / GRAIN) + 31) / 32 * 4);
    class_iterate(mark_class, 0);
    strings_scan_roots(mark_one);
    for (unsigned i = 0; i < pin_count; i++) mark(*pins[i]);
    drain();
    threads_scan_roots(mark_range);
    drain();
    sweep();
    since_gc = 0;
    gc_threshold = (page_count * PAGE - used_bytes) / 2;
    if (gc_threshold < (4U << 20)) gc_threshold = 4U << 20;
    if (vm_verbose)
        vm_printf(1, "[GC %uK->%uK of %uK, %u ms]\n", before >> 10, used_bytes >> 10,
                  page_count * PAGE >> 10, (unsigned)(plat_millis() - start));
    collecting = 0;
}

/* ---- typed allocation ---- */
Object *new_object(Thread *t, Class *c)
{
    Object *o = heap_alloc(t, c->instance_size);
    if (!o) { throw_new(t, "java/lang/OutOfMemoryError", "Java heap space"); return 0; }
    o->cls = c;
    return o;
}

Array *new_array(Thread *t, Class *array_class, jint length)
{
    if (length < 0) { throw_new(t, "java/lang/NegativeArraySizeException", 0); return 0; }
    uint64_t bytes = sizeof(Array) + (uint64_t)length * array_class->elem_size;
    if (bytes > 0x7ffffff0U) { throw_new(t, "java/lang/OutOfMemoryError", "Requested array size exceeds VM limit"); return 0; }
    Array *a = (Array *)heap_alloc(t, (uint32_t)bytes);
    if (!a) { throw_new(t, "java/lang/OutOfMemoryError", "Java heap space"); return 0; }
    a->o.cls = array_class;
    a->length = length;
    return a;
}

Array *new_prim_array(Thread *t, int type, jint length) { return new_array(t, vm.prim_arrays[type], length); }

Array *new_object_array(Thread *t, Class *element, jint length)
{
    Class *ac = array_of(t, element);
    return ac ? new_array(t, ac, length) : 0;
}

Object *clone_object(Thread *t, Object *o)
{
    uint32_t bytes = o->cls->flags & CF_ARRAY
        ? sizeof(Array) + (uint32_t)((Array *)o)->length * o->cls->elem_size : o->cls->instance_size;
    Object *c = heap_alloc(t, bytes);
    if (!c) { throw_new(t, "java/lang/OutOfMemoryError", "Java heap space"); return 0; }
    memcpy(c, o, bytes);
    c->monitor = 0;
    return c;
}
