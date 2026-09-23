#include "net.h"

static inline unsigned int port_inl(unsigned short port)
{ unsigned int v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(port)); return v; }
static inline void port_outl(unsigned short port, unsigned int value)
{ __asm__ volatile("outl %0,%1" : : "a"(value), "Nd"(port)); }

/* AMD PCnet-PCI reset sequence. */
int pcnet_reset(unsigned short io_base)
{
    if (!io_base) return -1;
    (void)port_inl(io_base + 0x10);
    port_outl(io_base + 0x14, 0x00000004);
    return 0;
}
