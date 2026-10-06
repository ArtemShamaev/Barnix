#ifndef DISK_H
#define DISK_H
#include <stdint.h>
#define DISK_SECTOR_SIZE 512
/* Decimal conversion without a compiler runtime 64-bit division helper. */
static inline void disk_u64toa(uint64_t value, char *out)
{
    char reverse[20]; unsigned int n = 0;
    do {
        uint64_t quotient = 0; unsigned int remainder = 0;
        for (int bit = 63; bit >= 0; bit--) {
            remainder = remainder * 2 + ((value >> bit) & 1);
            if (remainder >= 10) { remainder -= 10; quotient |= (uint64_t)1 << bit; }
        }
        reverse[n++] = '0' + remainder; value = quotient;
    } while (value);
    unsigned int i = 0; while (n) out[i++] = reverse[--n]; out[i] = 0;
}
#define DISK_MAX_DEVICES 64
#define DISK_PCI_BASE 64
int disk_device_count(void);
const char *disk_device_name(int index);
const char *disk_type(void);
uint64_t disk_capacity(void);
enum { DISK_NONE, DISK_RAM, DISK_ATA, DISK_USB };
typedef struct { int id; unsigned int offset, sectors, generation; } DiskSelection;
int disk_select_raw(const char *name);
int disk_select(const char *name);
DiskSelection disk_selection(void);
void disk_restore(DiskSelection selection);
void disk_deselect(void);
const char *disk_name(void);
int disk_present(void);
int disk_flush(void);
int disk_read_many(unsigned int lba, void *buffer, unsigned int count);
void disk_list(void);
int disk_init(void);
int disk_init_from_memory(const void *data, unsigned int size);
int disk_read(unsigned int lba, void *buffer);
int disk_write(unsigned int lba, const void *buffer);
void disk_info(void);
#endif
