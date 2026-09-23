#include "barnix.h"
#include "fs.h"
#include "text.h"
#include "lang.h"
#include "elf.h"
#include "disk.h"
#include "multiboot.h"
#include "net.h"
#include "linux_idt.h"
#include "linux_tss.h"
#include "linux_exec.h"
__attribute__((section(".multiboot"), used))
const unsigned int multiboot_header[] = {
    0x1BADB002,
    0,
    -(0x1BADB002)
};
void bssh_main(void);


static int prepare_live(const MultibootInfo *info)
{
    if (!(info->flags & MULTIBOOT_MODULES) || !info->module_count || !info->modules)
        return -1;
    const MultibootModule *module = (const MultibootModule *)info->modules;
    if (module->end <= module->start ||
        disk_init_from_memory((const void *)module->start, module->end - module->start))
        return -1;
    return 0;
}

void kernel_main(unsigned int magic, const MultibootInfo *info)
{
    console_font_init();
#ifdef BARNIX_LINUX_SELFTEST
    if (magic == MULTIBOOT_BOOT_MAGIC && info &&
        (info->flags & MULTIBOOT_CMDLINE) && info->command_line &&
        strstr((const char *)info->command_line, "linux-selftest"))
    {
        if (prepare_live(info) || disk_select("ram0") || fs_init())
            panic("Linux selftest requires a compatible ext2 module");
        linux_abi_selftest();
    }
#endif
    /* Linux task CPU state is installed only while linux_run is active. */
    net_init();
    clear();

    println(CYAN, tr("Barnix OS starting..."));

    if (magic != MULTIBOOT_BOOT_MAGIC || !info)
        panic(tr("Multiboot boot information missing"));

    int live_only = (info->flags & MULTIBOOT_CMDLINE) && info->command_line &&
                    strcmp((const char *)info->command_line, "live") == 0;
    int have_live = prepare_live(info) == 0;
    if (have_live && !disk_select("ram0") && !fs_init()) elf_cache_recovery();
    int ready = !live_only && disk_init() == 0 && fs_init() == 0;
    if (!ready)
    {
        if (!have_live || disk_select("ram0") || fs_init())
            panic(tr("No supported ext2 disk or CD/DVD filesystem module"));
        println(YELLOW, tr("CD/DVD live mode: changes are stored in RAM only"));
    }

    if (!have_live) elf_cache_recovery();
    system_init();
    println(GREEN, tr("fs ready"));

    println(YELLOW, tr("starting shell..."));

    bssh_main();

    while (1)
        __asm__ volatile("hlt");
}
