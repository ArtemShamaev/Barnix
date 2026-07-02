#include "disk.h"
#include "barnix.h"

#define DISK_SECTORS 4096

static unsigned char ram_disk[DISK_SECTORS][DISK_SECTOR_SIZE];

int disk_init(void)
{
    memset(ram_disk, 0, sizeof(ram_disk));
    return 0;
}

int disk_read(unsigned int lba, void *buffer)
{
    if (lba >= DISK_SECTORS)
        return -1;

    memcpy(buffer, ram_disk[lba], DISK_SECTOR_SIZE);
    return 0;
}

int disk_write(unsigned int lba, const void *buffer)
{
    if (lba >= DISK_SECTORS)
        return -1;

    memcpy(ram_disk[lba], buffer, DISK_SECTOR_SIZE);
    return 0;
}
