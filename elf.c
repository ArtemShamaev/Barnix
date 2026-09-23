#include "elf.h"
#include "lang.h"
#include "elf_format.h"
#include "fs.h"
#include "barnix.h"
#include "disk.h"
#include "keyboard.h"
#include "net.h"
#include "linux_exec.h"
void set_cursor(int x, int y);

static unsigned char executable[FS_MAX_FILE_SIZE];
static int running;
static void app_puts(const char *text) { println(WHITE, text); }
static const BarnixAPI api = {
    BARNIX_APP_ABI, app_puts, fs_size, fs_read, fs_write, fs_append,
    print, println, clear, panic, fs_ls, fs_pwd, fs_df, disk_info,
    disk_list, fs_mount_info, fs_touch, fs_rm, fs_mkdir, fs_cd,
    fs_rmdir, fs_stat, fs_cp, fs_mv, fs_mount, fs_unmount, fs_sync, system_init, tr,
    getch, set_cursor, net_connect_wifi, net_connect_cable, net_scan,
    net_ping, net_download, net_state
};
extern int elf_enter(unsigned int entry, const BarnixAPI *interface,
                     int argc, const char *const *argv, void *stack_top);

/* Recovery still executes ELF files, never shell builtins. Only retain the
 * initial copies of device-management tools, for an unmounted/legacy root. */
static const char *recovery_names[] = {"/bin/mount", "/bin/unmount", "/bin/devices"};
static unsigned char recovery[3][32768];
static int recovery_sizes[3];
void elf_cache_recovery(void)
{
    for (int i = 0; i < 3; i++) {
        int size = fs_size(recovery_names[i]);
        ElfPlan plan;
        if (size > 0 && size <= (int)sizeof(recovery[i]) &&
            fs_read(recovery_names[i], 0, recovery[i], size) == size &&
            !elf_validate(recovery[i], size, &plan)) recovery_sizes[i] = size;
    }
}

int elf_run(const char *name, int argc, const char *const *argv, int *status)
{
    if (running) { println(RED, tr("nested execution is unsupported")); return -1; }
    int size = fs_size(name);
    if (size < 0) {
        for (int i = 0; i < 3; i++) {
            if (!strcmp(name, recovery_names[i]) && recovery_sizes[i]) {
                size = recovery_sizes[i]; memcpy(executable, recovery[i], size); break;
            }
        }
        if (size < 0) { println(RED, tr("command not found: cannot read executable")); return -1; }
    } else if (size > FS_MAX_FILE_SIZE || fs_read(name, 0, executable, size) != size) {
        println(RED, tr("cannot read executable")); return -1;
    }
    ElfPlan plan;
    const char *error = elf_validate(executable, size, &plan);
    if (error) { print(RED, "ELF: "); println(RED, error); return -1; }
    if (!plan.legacy) return linux_run_image(executable, size, argc, argv, status);
    running = 1;
    /* The linker reserves this entire arena, including the application's stack,
     * so neither the kernel nor GRUB's filesystem module can occupy it. */
    memset((void *)APP_LOAD_MIN, 0, APP_STACK_TOP - APP_LOAD_MIN);
    for (unsigned int i = 0; i < plan.count; i++)
    {
        ElfSegment *s = &plan.segments[i];
        memcpy((void *)s->address, executable + s->offset, s->file_size);
    }
    *status = elf_enter(plan.entry, &api, argc, argv, (void *)APP_STACK_TOP);
    running = 0;
    return 0;
}
