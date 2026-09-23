/* Minimal freestanding C runtime needed by GCC-generated code. */
#include <stddef.h>
void *memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *d = destination; const unsigned char *s = source;
    while (size--) *d++ = *s++;
    return destination;
}
void *memset(void *destination, int value, size_t size)
{
    unsigned char *d = destination;
    while (size--) *d++ = value;
    return destination;
}
void *memmove(void *destination, const void *source, size_t size)
{
    unsigned char *d = destination; const unsigned char *s = source;
    if (d < s) while (size--) *d++ = *s++;
    else { d += size; s += size; while (size--) *--d = *--s; }
    return destination;
}
int memcmp(const void *a, const void *b, size_t size)
{
    const unsigned char *x = a, *y = b;
    while (size--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
