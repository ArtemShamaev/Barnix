#ifndef BARNIX_LINUX_TSS_H
#define BARNIX_LINUX_TSS_H
#include <stdint.h>
void linux_tss_init(void);
int linux_tss_set_thread_area(unsigned int address);
uint64_t linux_tss_tls_get(void);
void linux_tss_tls_set(uint64_t descriptor);
#endif
