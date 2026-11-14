/* Minimal freestanding C runtime needed by GCC-generated code. */
#include <stddef.h>
/* String instructions move four bytes per step: Java's System.arraycopy,
 * the OpenGL rasterizer and Gouse copy megabytes per frame through these. */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *destination, const void *source, size_t size)
{
    if (size < 32) {
        /* A few words, as the interpreter's call arguments: rep's start-up cost
         * would dominate. */
        unsigned int *dw = destination; const unsigned int *sw = source;
        for (; size >= 4; size -= 4) *dw++ = *sw++;
        unsigned char *db = (unsigned char *)dw; const unsigned char *sb = (const unsigned char *)sw;
        while (size--) *db++ = *sb++;
        return destination;
    }
    void *d = destination; const void *s = source;
    size_t words = size >> 2, bytes = size & 3;
    __asm__ volatile("cld; rep movsl; mov %3, %%ecx; rep movsb"
                     : "+D"(d), "+S"(s), "+c"(words) : "r"(bytes) : "memory");
    return destination;
}
void *memset(void *destination, int value, size_t size)
{
    void *d = destination;
    unsigned int pattern = (unsigned char)value * 0x01010101U;
    size_t words = size >> 2, bytes = size & 3;
    __asm__ volatile("cld; rep stosl; mov %3, %%ecx; rep stosb"
                     : "+D"(d), "+c"(words) : "a"(pattern), "r"(bytes) : "memory");
    return destination;
}
void *memmove(void *destination, const void *source, size_t size)
{
    unsigned char *d = destination; const unsigned char *s = source;
    if (d <= s || d >= s + size) return memcpy(destination, source, size);
    /* Overlapping, destination above: copy backwards. */
    size_t bytes = size & 3, words = size >> 2;
    d += size - 1; s += size - 1;
    __asm__ volatile("std; rep movsb; sub $3, %%esi; sub $3, %%edi; mov %3, %%ecx; rep movsl; cld"
                     : "+D"(d), "+S"(s), "+c"(bytes) : "r"(words) : "memory");
    return destination;
}
int memcmp(const void *a, const void *b, size_t size)
{
    const unsigned char *x = a, *y = b;
    while (size--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
