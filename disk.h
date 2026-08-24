#ifndef DISK_H
#define DISK_H
#define DISK_SECTOR_SIZE 512
int disk_init(void);
int disk_init_from_memory(const void *image, unsigned int size);
int disk_read(unsigned int lba, void *buffer);
int disk_write(unsigned int lba, const void *buffer);
#endif
