#include "barnix.h"
#include "fs.h"
#include "disk.h"
void bssh_main(void);

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_HAS_MODS 0x00000008

typedef struct
{
    unsigned int mod_start;
    unsigned int mod_end;
    unsigned int string;
    unsigned int reserved;
} MultibootModule;

typedef struct
{
    unsigned int flags;
    unsigned int mem_lower;
    unsigned int mem_upper;
    unsigned int boot_device;
    unsigned int cmdline;
    unsigned int mods_count;
    unsigned int mods_addr;
} MultibootInfo;

static int init_root_device(unsigned int magic, const MultibootInfo *mbi)
{
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC &&
        mbi &&
        (mbi->flags & MULTIBOOT_INFO_HAS_MODS) &&
        mbi->mods_count > 0)
    {
        MultibootModule *module = (MultibootModule *)mbi->mods_addr;
        unsigned int size = module->mod_end - module->mod_start;

        if (size > 0 && disk_init_from_memory((const void *)module->mod_start, size) == 0)
        {
            println(GREEN, "initrd module loaded");
            return 0;
        }
    }

    println(YELLOW, "no initrd module, using disk device");
    return disk_init();
}

void kernel_main(unsigned int magic, const MultibootInfo *mbi)
{
    clear();

    println(CYAN, "Barnix OS starting...");

    /* 1. root block device first */
    if (init_root_device(magic, mbi) != 0)
    {
        println(RED, "disk init failed");
        while (1);
    }

    println(GREEN, "root device ready");

    /* 2. fs after disk */
    if (fs_init() != 0)
    {
        println(RED, "fs init failed");
        while (1);
    }

    println(GREEN, "fs ready");

    println(YELLOW, "starting shell...");

    bssh_main();

    while (1)
        __asm__ volatile("hlt");
}
