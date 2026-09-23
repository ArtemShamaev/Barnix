#ifndef DISK_H
#define DISK_H
#define DISK_SECTOR_SIZE 512
enum { DISK_NONE, DISK_RAM, DISK_ATA, DISK_USB };
typedef struct { int id; unsigned int offset, sectors, generation; } DiskSelection;
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
