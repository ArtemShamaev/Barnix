#include "fault.h"
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
#include "smp.h"
#include "audio.h"
#include "system_sound.h"
#include "console_video.h"
#include "linux_exec.h"
#include "video_rgb.h"
#include "memory.h"
#include "mouse.h"
#include "linux_limits.h"
void set_cursor(int x, int y);

/* Keep the legacy arena at 4 MiB, but allow Linux ELF files with larger
 * on-disk sections when extended RAM is available. */
static unsigned char fallback_executable[4 * 1024 * 1024];
static unsigned char *executable;
static unsigned int executable_capacity;
static int running, launching;
static int run_as_root;   /* app_sudo: the next run() as root */
static char launch_path[1024];   /* "program arguments" (ABI 14: arguments allowed) */
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
static int app_run(const char *path, int argc, const char *const *argv, char *output, unsigned int capacity, void *swap, int *status);
static void *app_memory(unsigned int *bytes);
static int app_present(const unsigned int *pixels, unsigned int w, unsigned int h);
static int app_file_mode(const char *path, unsigned int mode);
static void app_run_area(int x, int y, int w, int h);
static int app_sudo(const char *password);
static int app_launch(const char *path) {
    if(!running||launching||!path||!path[0]||launch_path[0])return -1;
    unsigned int length=strlen(path);
    if(length>=sizeof(launch_path))return -1;
    char program[256];unsigned int n=0;
    while(path[n]&&path[n]!=' '&&n+1<sizeof(program)){program[n]=path[n];n++;}
    program[n]=0;
    if(fs_exec_check(program))return -1;
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
    set_file_permissions, theme_set, fs_fdisk, mouse_get, console_blit, console_graphic_blit, console_glyph_row, mouse_pointer, fs_getcwd, app_directory, app_launch,
    app_present, video_display, keyboard_key_event, mouse_motion, app_memory,
    smp_cpus, smp_parallel, audio_device, audio_speaker, audio_play, audio_stop, audio_playing, audio_volume,
    net_open, net_send, net_recv, net_close, net_resolve, net_info, net_ping_host, net_random,
    app_run, clipboard_set, clipboard_get, clipboard_serial, app_file_mode, app_run_area, app_sudo, fs_space
};
static const BarnixAPI *program_api = &api;   /* the table the next program gets */
extern int elf_enter(unsigned int entry, const BarnixAPI *interface,
                     int argc, const char *const *argv, void *stack_top);

/* Recovery still executes ELF files, never shell builtins. Only retain the
 * initial copies of device-management tools, for an unmounted/legacy root. */
static const char *recovery_names[] = {"/usr/bin/mount", "/usr/bin/unmount", "/usr/bin/devices", "/usr/bin/format", "/usr/bin/fdisk"};
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

/* The program file's buffer: 16 MiB (Python is 7), larger while it is the
 * newest kernel block; RAM goes to programs, not to a 64 MiB maximum. */
static int executable_fit(unsigned int bytes)
{
    if (executable == fallback_executable) { executable = 0; executable_capacity = 0; }
    void *block = executable;
    int error = memory_kernel_fit(&block, &executable_capacity, bytes);
    executable = block;
    if (!executable) { executable = fallback_executable; executable_capacity = sizeof(fallback_executable); }
    return error;
}
static void executable_prepare(void)
{
    if (executable && executable != fallback_executable) return;
    executable_fit(sizeof(fallback_executable));   /* retried after a failure */
}
/* memory() for the outer program. When GCC is installed and RAM allows,
 * the compiler's arenas are taken first, so the IDE and Gouse Prompt can
 * run it later without touching the program's block. */
#define TOOLCHAIN_BYTES (4U * LINUX_LARGE_ARENA_SIZE + 2U * FS_MAX_EXECUTABLE_SIZE)
#define INTERPRETER_BYTES LINUX_LARGE_ARENA_SIZE   /* Python: no processes, no exec buffer */
static unsigned int *compose, compose_capacity; /* a copy of the frame for run_area */
static const unsigned int *last_frame;          /* what the outer program presented last */
static unsigned int last_w, last_h;
static int area_x, area_y, area_w, area_h;      /* run_area, in frame pixels */
static char outer_program[64];   /* the program app_memory() is for */
static int keeps_toolchain(void)
{
    /* Happy Gouse runs compilers and Python inside itself (ABI 13 run()) */
    unsigned int n = strlen(outer_program);
    return n >= 5 && !strcmp(outer_program + n - 5, "gouse") && (n == 5 || outer_program[n - 6] == '/');
}
static void *app_memory(unsigned int *bytes)
{
    /* A copy of the frame for programs shown in a window (run_area). */
    if (!compose && memory_available() >= (96U << 20)) {
        compose = memory_kernel_alloc(1920U * 1200U * 4U);
        if (compose) compose_capacity = 1920U * 1200U;
    }
    if (!keeps_toolchain()) {
        /* Any other program (BJVM with Minecraft) gets that memory too. */
        linux_toolchain_release();
    } else if (memory_available() >= TOOLCHAIN_BYTES + (256U << 20) && fs_size("/usr/bin/gcc") >= 0) {
        executable_prepare();
        linux_toolchain_prepare(1);
    } else if (memory_available() >= INTERPRETER_BYTES + (112U << 20) && fs_size("/usr/bin/python3") >= 0) {
        executable_prepare();   /* 512 MiB of RAM: Python still runs in Gouse Prompt and Barnix Code */
        linux_toolchain_prepare(0);
    }
    return memory_app(bytes);
}
static int nested, nested_shown;
static int run_once(const char *name, int argc, const char *const *argv, int *status)
{
    if (running) { println(RED, tr("nested execution is unsupported")); return -1; }
    if (!nested) {
        unsigned int n = strlen(name);
        if (n >= sizeof(outer_program)) n = sizeof(outer_program) - 1;
        memcpy(outer_program, name, n); outer_program[n] = 0;
    }
    executable_prepare();
    int size = fs_load_executable(name, executable, executable_capacity);
    if (size == -5) {
        char full[256];
        unsigned int n = strlen(name);
        if (n < sizeof(full)) {
            memcpy(full, name, n + 1);
            fs_executable_path(full, sizeof(full));
            int bytes = fs_file_size(full);
            if (bytes > (int)executable_capacity && bytes <= (int)FS_MAX_EXECUTABLE_SIZE && !executable_fit(bytes))
                size = fs_load_executable(name, executable, executable_capacity);
        }
    }
    if (size < 0) {
        if (!permissions_root()) {
            print(RED,size==-2?"Command not found: ":size==-13?"Permission denied: ":"Cannot load executable: ");
            println(RED,name);system_sound(SYSTEM_SOUND_ERROR);return -1;
        }
        for (int i = 0; i < 5; i++) {
            if (!strcmp(name, recovery_names[i]) && recovery_sizes[i]) {
                size = recovery_sizes[i]; memcpy(executable, recovery[i], size); break;
            }
        }
        if (size < 0) { println(RED, tr("command not found: cannot read executable")); system_sound(SYSTEM_SOUND_ERROR); return -1; }
    }
    ElfPlan plan;
    const char *error = elf_validate(executable, size, &plan);
    if (error) { print(RED, "ELF: "); println(RED, error); return -1; }
    if (!plan.legacy) return linux_run_image(executable, size, argc, argv, status);
    if (!permissions_root()) {
        char cwd[256],canonical[512];
        if(name[0]=='/')strcpy(canonical,name);
        else {if(fs_getcwd(cwd,sizeof(cwd))<0)return -1;strcpy(canonical,cwd);strcat(canonical,"/");strcat(canonical,name);}
        if((strncmp(canonical,"/usr/bin/",9)&&strncmp(canonical,"/bin/",5))||strstr(canonical,"/../")||strstr(canonical,"/./")) {
            println(RED,"Native ABI programs must be installed in /usr/bin by root; use Linux ELF for user programs");return -1;
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
    BarnixAPI compatibility = *program_api;
    compatibility.version=executable[8];
    keyboard_events_reset();
    mouse_motion(0, 0);
    unsigned int image_end = APP_LOAD_MIN;
    for (unsigned int i = 0; i < plan.count; i++)
        if (plan.segments[i].address + plan.segments[i].memory_size > image_end)
            image_end = plan.segments[i].address + plan.segments[i].memory_size;
    unsigned int paging = fault_guard_enter(image_end);
    *status = elf_enter(plan.entry, &compatibility, argc, argv, (void *)APP_STACK_TOP);
    fault_guard_leave(paging);
    console_ansi_reset();
    if (!nested) { video_present(0, 0, 0); audio_reset(); memory_app_release(); last_frame = 0; area_w = area_h = 0; run_as_root = 0; }
    else audio_stop(-1);
    net_reset();
    running = 0;
    return 0;
}

/* ---------------------------------------------------------------- ABI 13 run() */
/* The caller's arena goes to its swap memory, the program runs in the
 * arena on a kernel stack (the caller's stack is part of the arena), and
 * the caller comes back as it was. What the program prints is collected;
 * one that needs the keyboard or the screen gets the text console, or
 * (ABI 14 run_area) a rectangle of the caller's last frame: a window. */
extern unsigned int saved_kernel_stack;
static unsigned char nested_stack[65536] __attribute__((aligned(16)));
static char nested_output[65536];
static BarnixAPI nested_api;
static struct {
    char path[256], strings[2048];
    const char *argv[32];
    int argc, status, result;
    unsigned int length;
    void *swap;
} job;

/* ---- the window: the caller's frame with the program drawn in an area */
static int windowed, window_tui, window_dirty;
static unsigned int window_shown_at;
static unsigned short window_seen[80 * 25];
static int window_seen_cursor;
static const unsigned int window_palette[16] = {
    0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
    0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff
};
static int app_present(const unsigned int *pixels, unsigned int w, unsigned int h)
{
    if (!nested) { last_frame = pixels; last_w = pixels ? w : 0; last_h = pixels ? h : 0; }
    return video_present(pixels, w, h);
}
static void app_run_area(int x, int y, int w, int h)
{
    if (nested) return;
    area_x = x; area_y = y; area_w = w; area_h = h;
}
static int app_sudo(const char *password)
{
    if (nested) return -13;
    if (!permissions_root() && (!password || authenticate_user("root", password))) { system_sound(SYSTEM_SOUND_ERROR); return -13; }
    run_as_root = 1;
    return 0;
}
static int app_file_mode(const char *path, unsigned int mode)
{
    if (!permissions_root()) return -13;
    return fs_chmod(path, mode & 07777U);
}
static void window_fill(int x, int y, int w, int h, unsigned int color)
{
    for (int j = 0; j < h; j++) {
        unsigned int *row = compose + (unsigned int)(y + j) * last_w + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}
static void window_show(int force)
{
    static int shown_first = -1;
    unsigned int now = net_ms();
    if (!force && now - window_shown_at < 40) { window_dirty = 1; return; }
    window_shown_at = now; window_dirty = 0;
    /* The cells that changed since the last picture. A smaller area shows
     * the rows around the cursor. */
    const unsigned short *cells = console_cells;
    int columns = (area_w - 8) / 8, rows = (area_h - 8) / 16;
    if (columns > 80) columns = 80;
    if (rows > 25) rows = 25;
    int cursor_row = window_seen_cursor >= 0 ? window_seen_cursor / 80 : 24;
    int first = cursor_row + 1 - rows;
    if (first < 0) first = 0;
    if (first != shown_first) { force = 1; shown_first = first; }
    int ox = area_x + 4 + (area_w - 8 - columns * 8) / 2, oy = area_y + 4;
    for (int r = 0; r < rows; r++)
        for (int col = 0; col < columns; col++) {
            int i = (first + r) * 80 + col;
            unsigned int cell = cells[i];
            if (!force && cell == window_seen[i]) continue;
            window_seen[i] = (unsigned short)cell;
            unsigned int fg = window_palette[(cell >> 8) & 15], bg = window_palette[(cell >> 12) & 7];
            for (int y = 0; y < 16; y++) {
                unsigned int bits = console_glyph_row(cell & 255, (unsigned int)y);
                if (i == window_seen_cursor && y >= 14) bits = 255;
                unsigned int *out = compose + (unsigned int)(oy + r * 16 + y) * last_w + ox + col * 8;
                for (int x = 0; x < 8; x++) out[x] = bits & (128U >> x) ? fg : bg;
            }
        }
    video_present(compose, last_w, last_h);
}
static void window_text(const unsigned short *cells, int cursor)
{
    (void)cells;
    if (cursor != window_seen_cursor) {   /* both places are drawn again */
        if (window_seen_cursor >= 0) window_seen[window_seen_cursor] = 0xffff;
        if (cursor >= 0) window_seen[cursor] = 0xffff;
        window_seen_cursor = cursor;
    }
    window_show(0);
}
/* A picture of the program, fitted into the area. */
static void window_picture(const unsigned int *pixels, const unsigned char *indexed, unsigned int w, unsigned int h)
{
#define PIXEL(k) (pixels ? pixels[k] : window_palette[indexed[k] & 15])
    unsigned int aw = (unsigned int)area_w, ah = (unsigned int)area_h, up = 1, down = 1;
    if (w <= aw && h <= ah) { up = aw / w; if (ah / h < up) up = ah / h; }
    else { down = (w + aw - 1) / aw; if ((h + ah - 1) / ah > down) down = (h + ah - 1) / ah; }
    unsigned int ow = w * up / down, oh = h * up / down;
    int ox = area_x + (int)(aw - ow) / 2, oy = area_y + (int)(ah - oh) / 2;
    window_fill(area_x, area_y, area_w, area_h, 0);
    for (unsigned int y = 0; y < h / down; y++)
        for (unsigned int x = 0; x < w / down; x++) {
            unsigned int c = PIXEL(y * down * w + x * down);
            if (down > 1) {
                unsigned int r = 0, g = 0, b = 0, n = down * down;
                for (unsigned int j = 0; j < down; j++)
                    for (unsigned int i = 0; i < down; i++) {
                        unsigned int v = PIXEL((y * down + j) * w + x * down + i);
                        r += v >> 16 & 255; g += v >> 8 & 255; b += v & 255;
                    }
                c = (r / n) << 16 | (g / n) << 8 | b / n;
            }
            for (unsigned int j = 0; j < up; j++) {
                unsigned int *out = compose + (unsigned int)(oy + (int)(y * up + j)) * last_w + ox + x * up;
                for (unsigned int i = 0; i < up; i++) out[i] = c & 0xffffff;
            }
        }
    video_present(compose, last_w, last_h);
#undef PIXEL
}
static void window_begin(void)
{
    console_capture_echo(1);   /* what it prints is shown and still collected */
    console_window(window_text);
    window_fill(area_x, area_y, area_w, area_h, 0);
    for (int i = 0; i < 2000; i++) window_seen[i] = 0xffff;
    window_seen_cursor = -1;
    console_capture_pause(1);   /* what was collected is shown once more, not collected twice */
    clear();
    nested_output[console_captured()] = 0;
    char *text = nested_output;
    for (char *p = nested_output; *p; p++) if (*p == '\f') text = p + 1;
    print(WHITE, text);
    console_capture_pause(0);
    window_show(1);
}

static void nested_reveal(int text)
{
    if (nested_shown) return;
    nested_shown = 1;
    if (windowed) {
        if (!text) { job.length = 0; console_capture(0, 0); window_tui = 1; return; }
        window_begin();
        return;
    }
    job.length = console_captured();
    console_capture(0, 0);
    if (!text) return;
    video_present(0, 0, 0);   /* back to the text console */
    clear();
    nested_output[job.length] = 0;
    for (unsigned int i = 0; i < job.length; i++) if (nested_output[i] == '\f') nested_output[i] = '\n';
    print(WHITE, nested_output);
}
/* A full-screen text program (cursor positioning, cell blits) leaves no
 * useful transcript: only what it printed before is handed back. */
static void nested_tui(void)
{
    if (!windowed || window_tui) return;
    window_tui = 1;
    job.length = 0;              /* a full-screen program leaves no transcript */
    console_capture_pause(1);    /* and is shown, not collected */
}
static unsigned int nested_getch(void)
{
    nested_reveal(1);
    if (windowed && window_dirty) window_show(1);
    return getch();
}
static unsigned int nested_poll_key(void)
{
    if (windowed && window_dirty) window_show(0);
    return nested_shown ? keyboard_poll() : 0;
}
static int nested_key_event(BarnixKeyEvent *event)
{
    if (windowed && window_dirty) window_show(0);
    return nested_shown ? keyboard_key_event(event) : 0;
}
static void nested_goto_xy(int x, int y) { nested_reveal(1); nested_tui(); set_cursor(x, y); }
static void nested_screen(const unsigned short *cells) { nested_reveal(1); nested_tui(); console_blit(cells); }
static int nested_graphics(const unsigned char *pixels)
{
    if (pixels) nested_reveal(0);
    if (!windowed) return console_graphic_blit(pixels);
    if (!pixels) return 0;
    window_picture(0, pixels, 640, 400);
    return 0;
}
static int nested_present(const unsigned int *pixels, unsigned int w, unsigned int h)
{
    if (pixels) nested_reveal(0);
    if (!windowed) return video_present(pixels, w, h);
    if (!pixels) return 0;   /* the window stays until the program ends */
    if (!w || !h || w > 4096 || h > 4096) return -1;
    window_picture(pixels, 0, w, h);
    return 0;
}
static int nested_display(unsigned int *w, unsigned int *h)
{
    if (!windowed) return video_display(w, h);
    if (w) *w = (unsigned int)area_w;
    if (h) *h = (unsigned int)area_h;
    return 0;
}
static void *nested_memory(unsigned int *bytes) { return memory_nested(bytes); }
static int nested_launch(const char *path) { (void)path; return -1; }
static int nested_run(const char *path, int argc, const char *const *argv, char *output, unsigned int capacity, void *swap, int *status)
{ (void)path; (void)argc; (void)argv; (void)output; (void)capacity; (void)swap; (void)status; return -1; }
/* A Linux program positions the cursor (ANSI): a full-screen program. */
void elf_nested_tui(void)
{
    if (!nested) return;
    nested_reveal(1);
    nested_tui();
}
/* A Linux program reads its standard input: it needs the keyboard. */
void elf_nested_input(void)
{
    if (!nested) return;
    nested_reveal(1);
    if (windowed && window_dirty) window_show(1);
}
/* A program in a window that keeps printing (downloads, builds) is shown
 * after a moment, so its progress is visible. */
static unsigned int job_started;
static void nested_watch(void)
{
    if (windowed && !nested_shown && net_ms() - job_started >= 300) nested_reveal(1);
}
static void nested_body(void)
{
    unsigned int saved = saved_kernel_stack;
    audio_stop(-1);   /* the caller's clips live in its arena */
    memcpy(job.swap, (void *)APP_LOAD_MIN, APP_STACK_TOP - APP_LOAD_MIN);
    nested = 1; nested_shown = 0; running = 0; window_tui = 0; window_dirty = 0;
    job.length = 0;
    console_capture(nested_output, sizeof(nested_output) - 1);
    job_started = net_ms();
    console_capture_watch(windowed ? nested_watch : 0);
    program_api = &nested_api;
    job.status = 0;
    char previous[64] = "";
    if (run_as_root && !permissions_root()) { strcpy(previous, get_current_user()); permissions_restore("root"); }
    char cwd[256];
    if (previous[0] && fs_getcwd(cwd, sizeof(cwd)) < 0) cwd[0] = 0;
    run_as_root = 0;
    job.result = run_once(job.path, job.argc, job.argv, &job.status);
    if (previous[0]) { permissions_restore(previous); if (cwd[0]) fs_cd(cwd); settings_load(); }
    program_api = &api;
    if (!nested_shown || (windowed && !window_tui)) job.length = console_captured();
    console_capture(0, 0);
    console_capture_watch(0);
    if (windowed) {
        if (console_windowed()) { console_capture_pause(1); console_finish_line(); console_capture_pause(0); console_window(0); }
        keyboard_events_reset();   /* the keys it read are not the caller's */
    } else if (nested_shown) {
        console_finish_line();
        println(WHITE, tr("Press a key to return"));
        while (getch() == KEY_MOUSE) {}
        video_present(0, 0, 0);
    }
    running = 1; nested = 0;
    saved_kernel_stack = saved;
    memcpy((void *)APP_LOAD_MIN, job.swap, APP_STACK_TOP - APP_LOAD_MIN);
}
static void on_stack(void (*fn)(void), void *top)
{
    __asm__ volatile("movl %%esp, %%ebx\n\t"
                     "movl %%edi, %%esp\n\t"
                     "pushl %%ebx\n\t"
                     "call *%%esi\n\t"
                     "popl %%esp"
                     :: "S"(fn), "D"(top) : "eax", "ebx", "ecx", "edx", "memory", "cc");
}
static int app_run(const char *path, int argc, const char *const *argv, char *output, unsigned int capacity, void *swap, int *status)
{
    if (!running || nested || !path || !swap || (unsigned int)swap < 0x02000000U || argc < 0 || argc > 31 ||
        strlen(path) >= (int)sizeof(job.path)) { run_as_root = 0; return -1; }
    strcpy(job.path, path);
    unsigned int used = 0;
    for (int i = 0; i < argc; i++) {   /* the arguments live in the caller's arena: copy them */
        unsigned int n = (unsigned int)strlen(argv[i]) + 1;
        if (used + n > sizeof(job.strings)) { run_as_root = 0; return -1; }
        memcpy(job.strings + used, argv[i], (int)n);
        job.argv[i] = job.strings + used;
        used += n;
    }
    job.argv[argc] = 0;
    job.argc = argc; job.swap = swap;
    if (!nested_api.version) {
        nested_api = api;
        nested_api.getch = nested_getch; nested_api.poll_key = nested_poll_key; nested_api.key_event = nested_key_event;
        nested_api.screen = nested_screen; nested_api.graphics = nested_graphics; nested_api.present = nested_present;
        nested_api.display = nested_display; nested_api.goto_xy = nested_goto_xy;
        nested_api.memory = nested_memory; nested_api.launch = nested_launch; nested_api.run = nested_run;
        nested_api.run_area = 0;
    }
    /* The window: the area must lie in the last frame, which is copied now
     * (it may live in the arena that goes to swap). */
    windowed = 0;
    if (area_w >= 64 && area_h >= 48 && last_frame && compose && area_x >= 0 && area_y >= 0 &&
        (unsigned int)(area_x + area_w) <= last_w && (unsigned int)(area_y + area_h) <= last_h &&
        last_w * last_h <= compose_capacity) {
        memcpy(compose, last_frame, (int)(last_w * last_h * 4));
        windowed = 1;
    }
    on_stack(nested_body, nested_stack + sizeof(nested_stack) - 16);
    windowed = 0;
    area_w = area_h = 0;   /* one run() only */
    unsigned int n = job.length < capacity ? job.length : capacity ? capacity - 1 : 0;
    if (output && capacity) { memcpy(output, nested_output, (int)n); output[n] = 0; }
    if (status) *status = job.status;
    return job.result < 0 ? job.result : (int)n;
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
        /* the words of the request: program, then arguments ("quoted" ones keep spaces) */
        static char words[1024];const char *child_argv[33];int child_argc=0;
        strcpy(words,launch_path);
        for(char *p=words;*p&&child_argc<32;){
            while(*p==' ')*p++=0;
            if(!*p)break;
            if(*p=='"'){child_argv[child_argc++]=++p;while(*p&&*p!='"')p++;if(*p)*p++=0;continue;}
            child_argv[child_argc++]=p;while(*p&&*p!=' ')p++;
        }
        child_argv[child_argc]=0;
        if(!child_argc){launch_path[0]=0;return -1;}
        {   /* a program that launches itself (Happy Gouse after a new screen size): just again */
            const char *a=name,*b=child_argv[0];
            for(const char *p=name;*p;p++)if(*p=='/')a=p+1;
            for(const char *p=child_argv[0];*p;p++)if(*p=='/')b=p+1;
            if(!strcmp(a,b))continue;
        }
        char cwd[256];
        if(fs_getcwd(cwd,sizeof(cwd))<0){launch_path[0]=0;return -1;}
        int child_status=0;
        launching=1;
        clear();
        int error=run_once(child_argv[0],child_argc,child_argv,&child_status);
        launching=0;
        console_finish_line();
        print(WHITE,error?"Launch failed: ":"Program exited: ");
        print_int(WHITE,error?error:child_status);
        println(WHITE,"  - Press a key to return");
        while(getch()==KEY_MOUSE){}
        if(fs_cd(cwd))fs_cd("/");
    }
}
