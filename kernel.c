#include "mouse.h"
#include "barnix.h"
#include "fs.h"
#include "text.h"
#include "lang.h"
#include "elf.h"
#include "disk.h"
#include "multiboot.h"
#include "console_video.h"
#include "storage_pci.h"
#include "net.h"
#include "linux_idt.h"
#include "linux_tss.h"
#include "linux_exec.h"
#ifdef BARNIX_SETUP
#include "Setup/setup.h"
#endif
__attribute__((section(".multiboot"), used))
const unsigned int multiboot_header[] = {
    0x1BADB002,
#ifdef BARNIX_EFI
    4, -(0x1BADB002 + 4),
    0, 0, 0, 0, 0, /* unused address fields */
    0, 800, 600, 32
#else
    0, -(0x1BADB002)
#endif
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

/* GRUB passes the UUID of the filesystem it actually booted. Device names
 * may change when the installed disk moves between IDE slots or USB/ATA. */
static int select_boot_uuid(const char *text)
{
    unsigned char uuid[16];
    int at = 0;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (text[i] != '-') return -1; continue; }
        unsigned int c = text[i], value;
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
        else return -1;
        if (!(at & 1)) uuid[at / 2] = value << 4;
        else uuid[at / 2] |= value;
        at++;
    }
    if (text[36] && text[36] != ' ') return -1;
    int count = disk_device_count();
    unsigned char super[512];
    for (int i = 0; i < count; i++) {
        const char *device=disk_device_name(i);
        for(int p=0;p<=4;p++) {
            char path[40];strcpy(path,device);
            if(p){int n=strlen(path);path[n]='p';path[n+1]='0'+p;path[n+2]=0;}
            if (!disk_select(path) && !disk_read(2, super) &&
                super[56] == 0x53 && super[57] == 0xef && !memcmp(super + 104, uuid, 16)) return 0;
        }
    }
    return -1;
}

void kernel_main(unsigned int magic, const MultibootInfo *info)
{
#ifdef BARNIX_EFI
    if (magic == MULTIBOOT_BOOT_MAGIC) storage_pci_boot_memory(info);
#endif
    if (magic == MULTIBOOT_BOOT_MAGIC) console_video_init(info);
    if (!console_graphics()) console_font_init();
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
    mouse_init();
    net_init();
    clear();

    println(CYAN, tr("Barnix OS starting..."));

    if (magic != MULTIBOOT_BOOT_MAGIC || !info)
        panic(tr("Multiboot boot information missing"));

    int live_only = (info->flags & MULTIBOOT_CMDLINE) && info->command_line &&
                    strcmp((const char *)info->command_line, "live") == 0;
    #ifdef BARNIX_SETUP
    live_only = 1;
    #endif
    int have_live = prepare_live(info) == 0;
    if (have_live && !disk_select("ram0") && !fs_init()) elf_cache_recovery();
    const char *root_uuid = NULL;
    if ((info->flags & MULTIBOOT_CMDLINE) && info->command_line)
        root_uuid = strstr((const char *)info->command_line, "root_uuid=");
    int ready = !live_only && (root_uuid ? select_boot_uuid(root_uuid + 10) : disk_init()) == 0 && fs_init() == 0;
    if (!live_only && root_uuid && !ready) panic("Installed root UUID not found or filesystem damaged");
    if (!ready)
    {
        if (!have_live || disk_select("ram0") || fs_init())
            panic(tr("No supported ext2 disk or CD/DVD filesystem module"));
        println(YELLOW, tr("CD/DVD live mode: changes are stored in RAM only"));
    }

    if (!have_live) elf_cache_recovery();
    system_init();
    println(GREEN, tr("fs ready"));

    #ifdef BARNIX_SETUP
    setup_run(info);
    clear();
    #endif
    println(YELLOW, tr("starting shell..."));

    bssh_main();

    while (1)
        __asm__ volatile("hlt");
}
