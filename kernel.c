#include "barnix.h"
#include "fs.h"
#include "disk.h"
#include "multiboot.h"
__attribute__((section(".multiboot"), used))
const unsigned int multiboot_header[] = {
    0x1BADB002,
    0,
    -(0x1BADB002)
};
void bssh_main(void);


static int mount_live(const MultibootInfo *info)
{
    if (!(info->flags & MULTIBOOT_MODULES) || !info->module_count || !info->modules)
        return -1;
    const MultibootModule *module = (const MultibootModule *)info->modules;
    if (module->end <= module->start ||
        disk_init_from_memory((const void *)module->start, module->end - module->start))
        return -1;
    if (fs_init()) return -1;
    println(YELLOW, "CD/DVD live mode: changes are stored in RAM only");
    return 0;
}

void kernel_main(unsigned int magic, const MultibootInfo *info)
{
    clear();

    println(CYAN, "Barnix OS starting...");

    if (magic != MULTIBOOT_BOOT_MAGIC || !info)
        panic("Multiboot boot information missing");

    int live_only = (info->flags & MULTIBOOT_CMDLINE) && info->command_line &&
                    strcmp((const char *)info->command_line, "live") == 0;
    int ready = !live_only && disk_init() == 0 && fs_init() == 0;
    if (!ready && mount_live(info) != 0)
        panic("No supported ext2 disk or CD/DVD filesystem module");

    println(GREEN, "fs ready");

    println(YELLOW, "starting shell...");

    bssh_main();

    while (1)
        __asm__ volatile("hlt");
}
