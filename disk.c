#include "disk.h"
#include "barnix.h"

#define DISK_SECTORS 4096

#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LOW    0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HIGH   0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_CONTROL    0x3F6

#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_FLUSH    0xE7

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_BSY 0x80

static unsigned char ram_disk[DISK_SECTORS][DISK_SECTOR_SIZE];
static int use_ata_disk;

static void init_ram_disk(void)
{
    use_ata_disk = 0;
    memset(ram_disk, 0, sizeof(ram_disk));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(unsigned short port, unsigned short value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void)
{
    outb(0x80, 0);
}

static int ata_wait_not_busy(void)
{
    for (int i = 0; i < 1000000; i++)
        if ((inb(ATA_STATUS) & ATA_SR_BSY) == 0)
            return 0;

    return -1;
}

static int ata_wait_drq(void)
{
    for (int i = 0; i < 1000000; i++)
    {
        unsigned char status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR)
            return -1;
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ))
            return 0;
    }

    return -1;
}

static int ata_select_master(void)
{
    outb(ATA_DRIVE, 0xE0);
    io_wait();
    return ata_wait_not_busy();
}

static int ata_identify(void)
{
    if (ata_select_master() != 0)
        return -1;

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();

    if (inb(ATA_STATUS) == 0)
        return -1;

    if (ata_wait_drq() != 0)
        return -1;

    for (int i = 0; i < 256; i++)
        (void)inw(ATA_DATA);

    return 0;
}

static int ata_prepare_lba(unsigned int lba)
{
    if (lba >= (1U << 28))
        return -1;

    if (ata_wait_not_busy() != 0)
        return -1;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    return 0;
}

int disk_init(void)
{
    use_ata_disk = (ata_identify() == 0);
    if (!use_ata_disk)
        init_ram_disk();

    return 0;
}

int disk_init_from_memory(const void *image, unsigned int size)
{
    unsigned int max_size = DISK_SECTORS * DISK_SECTOR_SIZE;

    init_ram_disk();

    if (size > max_size)
        size = max_size;

    memmove(ram_disk, image, size);
    return 0;
}

int disk_read(unsigned int lba, void *buffer)
{
    if (lba >= DISK_SECTORS)
        return -1;

    if (!use_ata_disk)
    {
        memcpy(buffer, ram_disk[lba], DISK_SECTOR_SIZE);
        return 0;
    }

    if (ata_prepare_lba(lba) != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_READ);
    if (ata_wait_drq() != 0)
        return -1;

    unsigned short *words = (unsigned short *)buffer;
    for (int i = 0; i < 256; i++)
        words[i] = inw(ATA_DATA);

    return 0;
}

int disk_write(unsigned int lba, const void *buffer)
{
    if (lba >= DISK_SECTORS)
        return -1;

    if (!use_ata_disk)
    {
        memcpy(ram_disk[lba], buffer, DISK_SECTOR_SIZE);
        return 0;
    }

    if (ata_prepare_lba(lba) != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_WRITE);
    if (ata_wait_drq() != 0)
        return -1;

    const unsigned short *words = (const unsigned short *)buffer;
    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, words[i]);

    if (ata_wait_not_busy() != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    return ata_wait_not_busy();
}
