/* Native support for QuickJS/musl. No host syscalls and no TLS dependency. */
#define hidden __attribute__((visibility("hidden")))
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "pthread_impl.h"
#include "locale_impl.h"
extern void *g_alloc(unsigned int);
extern unsigned int g_memory_free(void), g_now(void);
extern void web_progress(const char *);

typedef struct Block { size_t size; struct Block *next, *prev; unsigned int used; } Block;
static Block *heap;
static struct pthread thread;
uintptr_t bw_thread_pointer(void) {
    thread.self = &thread;
    thread.locale = (locale_t)&__c_locale;
    return (uintptr_t)&thread;
}
int bw_heap_init(void) {
    if (heap) return 1;
    unsigned int size = g_memory_free() / 3;
    if (size > (48u << 20)) size = 48u << 20;
    if (size < (8u << 20)) return 0;
    heap = g_alloc(size);
    if (!heap) return 0;
    heap->size = size - sizeof(*heap);
    /* Native applications execute in ring zero; the rest of Gouse uses integers. */
    unsigned int cr0;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    cr0 &= ~12u; cr0 |= 2u;
    __asm__ volatile("mov %0,%%cr0; fninit" :: "r"(cr0) : "memory");
    return 1;
}
void *malloc(size_t n) {
    if (!n) n = 1;
    if (n > 0x7ffffff0u) return 0;
    n = (n + 15) & ~15u;
    for (Block *b = heap; b; b = b->next) if (!b->used && b->size >= n) {
        if (b->size >= n + sizeof(Block) + 16) {
            Block *r = (Block *)((char *)(b + 1) + n);
            r->size = b->size - n - sizeof(Block); r->used = 0;
            r->next = b->next; r->prev = b;
            if (r->next) r->next->prev = r;
            b->next = r; b->size = n;
        }
        b->used = 1; return b + 1;
    }
    return 0;
}
void free(void *p) {
    if (!p) return;
    Block *b = (Block *)p - 1; b->used = 0;
    if (b->next && !b->next->used) {
        Block *r = b->next; b->size += sizeof(Block) + r->size;
        b->next = r->next; if (b->next) b->next->prev = b;
    }
    if (b->prev && !b->prev->used) {
        Block *l = b->prev; l->size += sizeof(Block) + b->size;
        l->next = b->next; if (l->next) l->next->prev = l;
    }
}
size_t malloc_usable_size(void *p) { return p ? ((Block *)p - 1)->size : 0; }
void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    size_t old = malloc_usable_size(p);
    if (n <= old) return p;
    void *r = malloc(n); if (!r) return 0;
    memcpy(r, p, old); free(p); return r;
}
void *calloc(size_t n, size_t size) {
    if (size && n > (size_t)-1 / size) return 0;
    void *p = malloc(n * size); if (p) memset(p, 0, n * size); return p;
}
int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    extern int bnet_platform_now(char out[15]);
    char text[15]; struct tm utc = {0};
    if (!bnet_platform_now(text)) {
        int fields[6]={0}; const int lengths[6]={4,2,2,2,2,2}; int at=0;
        for(int i=0;i<6;i++)for(int k=0;k<lengths[i];k++)fields[i]=fields[i]*10+text[at++]-'0';
        utc.tm_year=fields[0]-1900;utc.tm_mon=fields[1]-1;utc.tm_mday=fields[2];
        utc.tm_hour=fields[3];utc.tm_min=fields[4];utc.tm_sec=fields[5];
        tv->tv_sec=timegm(&utc);tv->tv_usec=0;
    } else { unsigned int now=g_now();tv->tv_sec=now/1000;tv->tv_usec=now%1000*1000; }
    return 0;
}
struct tm *localtime_r(const time_t *t, struct tm *out) { return gmtime_r(t, out); }
_Noreturn void abort(void) {
    web_progress("JavaScript runtime internal failure");
    /* Only an engine invariant failure reaches here, not a script exception. */
    for (;;) __asm__ volatile("hlt");
}
