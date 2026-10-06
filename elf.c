#include "elf.h"
#include "lang.h"
#include "elf_format.h"
#include "fs.h"
#include "barnix.h"
#include "disk.h"
#include "keyboard.h"
#include "net.h"
#include "permissions.h"
#include "settings.h"
#include "console_video.h"
#include "linux_exec.h"
void set_cursor(int x, int y);

static unsigned char executable[FS_MAX_FILE_SIZE];
static int running, launching;
static char launch_path[256];
static int app_directory(const char *path,unsigned int *position,BarnixDirEntry *entry) {
    /* Linux getdents requires room for one maximal ext2 name. Ask for a page,
     * return its first record, and advance only to that record's offset. */
    unsigned char buffer[512];
    if(!position||!entry)return -1;
    unsigned int cursor=*position;
    int n=fs_getdents(path,buffer,sizeof(buffer),&cursor);
    if(n<=0)return n;
    unsigned int length=buffer[8]|((unsigned int)buffer[9]<<8);
    if(length<12||length>(unsigned int)n)return -1;
    unsigned int i=0;
    while(i<255&&10+i<length-1&&buffer[10+i]){entry->name[i]=buffer[10+i];i++;}
    entry->name[i]=0;entry->kind=buffer[length-1];
    *position=(unsigned int)buffer[4]|((unsigned int)buffer[5]<<8)|((unsigned int)buffer[6]<<16)|((unsigned int)buffer[7]<<24);
    return 1;
}
static int app_launch(const char *path) {
    if(!running||launching||!path||!path[0]||launch_path[0])return -1;
    unsigned int length=strlen(path);
    if(length>=sizeof(launch_path))return -1;
    if(fs_exec_check(path))return -1;
    strcpy(launch_path,path);return 0;
}
static void app_puts(const char *text) { println(WHITE, text); }
static void app_panic(const char *text) { if(permissions_root())panic(text);else println(RED,"panic requires root"); }
static const BarnixAPI api = {
    BARNIX_APP_ABI, app_puts, fs_size, fs_read, fs_write, fs_append,
    print, println, clear, app_panic, fs_ls, fs_pwd, fs_df, fs_disk_info,
    disk_list, fs_mount_info, fs_touch, fs_rm, fs_mkdir, fs_cd,
    fs_rmdir, fs_stat, fs_cp, fs_mv, fs_mount, fs_unmount, fs_sync, system_init, tr,
    getch, set_cursor, net_connect_wifi, net_connect_cable, net_scan,
    net_ping, net_download, net_state, fs_format, fs_unmount_path, fs_lsblk, keyboard_poll, keyboard_sleep, get_current_user,
    set_file_permissions, theme_set, fs_fdisk, mouse_get, console_blit, console_graphic_blit, console_glyph_row, mouse_pointer, fs_getcwd, app_directory, app_launch
};
extern int elf_enter(unsigned int entry, const BarnixAPI *interface,
                     int argc, const char *const *argv, void *stack_top);

/* Recovery still executes ELF files, never shell builtins. Only retain the
 * initial copies of device-management tools, for an unmounted/legacy root. */
static const char *recovery_names[] = {"/bin/mount", "/bin/unmount", "/bin/devices", "/bin/format", "/bin/fdisk"};
static unsigned char recovery[5][32768];
static int recovery_sizes[5];
void elf_cache_recovery(void)
{
    for (int i = 0; i < 5; i++) {
        int size = fs_size(recovery_names[i]);
        ElfPlan plan;
        if (size > 0 && size <= (int)sizeof(recovery[i]) &&
            fs_read(recovery_names[i], 0, recovery[i], size) == size &&
            !elf_validate(recovery[i], size, &plan)) recovery_sizes[i] = size;
    }
}

static int run_once(const char *name, int argc, const char *const *argv, int *status)
{
    if (running) { println(RED, tr("nested execution is unsupported")); return -1; }
    int size = fs_load_executable(name, executable, sizeof(executable));
    if (size < 0) {
        if (!permissions_root()) {
            print(RED,size==-2?"Command not found: ":size==-13?"Permission denied: ":"Cannot load executable: ");
            println(RED,name);return -1;
        }
        for (int i = 0; i < 5; i++) {
            if (!strcmp(name, recovery_names[i]) && recovery_sizes[i]) {
                size = recovery_sizes[i]; memcpy(executable, recovery[i], size); break;
            }
        }
        if (size < 0) { println(RED, tr("command not found: cannot read executable")); return -1; }
    }
    ElfPlan plan;
    const char *error = elf_validate(executable, size, &plan);
    if (error) { print(RED, "ELF: "); println(RED, error); return -1; }
    if (!plan.legacy) return linux_run_image(executable, size, argc, argv, status);
    if (!permissions_root()) {
        char cwd[256],canonical[512];
        if(name[0]=='/')strcpy(canonical,name);
        else {if(fs_getcwd(cwd,sizeof(cwd))<0)return -1;strcpy(canonical,cwd);strcat(canonical,"/");strcat(canonical,name);}
        if(strncmp(canonical,"/bin/",5)||strstr(canonical,"/../")||strstr(canonical,"/./")) {
            println(RED,"Native ABI programs must be installed in /bin by root; use Linux ELF for user programs");return -1;
        }
    }
    running = 1;
    /* The linker reserves this entire arena, including the application's stack,
     * so neither the kernel nor GRUB's filesystem module can occupy it. */
    memset((void *)APP_LOAD_MIN, 0, APP_STACK_TOP - APP_LOAD_MIN);
    for (unsigned int i = 0; i < plan.count; i++)
    {
        ElfSegment *s = &plan.segments[i];
        memcpy((void *)s->address, executable + s->offset, s->file_size);
    }
    BarnixAPI compatibility = api;
    compatibility.version=executable[8];
    *status = elf_enter(plan.entry, &compatibility, argc, argv, (void *)APP_STACK_TOP);
    console_graphic_blit(NULL);
    running = 0;
    return 0;
}

/* Unwind the native application's stack before loading another image. This
 * keeps elf_enter's single saved kernel stack and the one application arena
 * intact. The launch request and cwd live in kernel memory, never in that arena. */
int elf_run(const char *name,int argc,const char *const *argv,int *status) {
    if(running||launching)return -1;
    for(;;){
        launch_path[0]=0;
        int result=run_once(name,argc,argv,status);
        if(result||*status||!launch_path[0]){launch_path[0]=0;return result;}
        char cwd[256];
        if(fs_getcwd(cwd,sizeof(cwd))<0){launch_path[0]=0;return -1;}
        const char *child_argv[]={launch_path};int child_status=0;
        launching=1;
        clear();
        int error=run_once(launch_path,1,child_argv,&child_status);
        launching=0;
        console_finish_line();
        print(WHITE,error?"Launch failed: ":"Program exited: ");
        print_int(WHITE,error?error:child_status);
        println(WHITE,"  - Press a key to return to Barnix Execute");
        while(getch()==KEY_MOUSE){}
        if(fs_cd(cwd))fs_cd("/");
    }
}
