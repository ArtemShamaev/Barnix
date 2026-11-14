#include "linux_exec.h"
#include "linux_user.h"
#include "linux_task.h"
#include "linux_idt.h"
#include "linux_tss.h"
#include "linux_memory.h"
#include "elf_format.h"
#include "barnix.h"
#include "fs.h"
#include "permissions.h"
#include "memory.h"
#include "linux_limits.h"

/* Staging is kernel-owned: exec must finish reading every user pointer before
 * replacing the address space. Cooperative tasks share this staging area. */
enum { VECTOR_MAX = 64, STRING_MAX = 8192, EXEC_PATH_MAX = 128 };
/* Executable files can include sections that are not mapped into the 4 MiB
 * process window. Use extended RAM for staging when it is available. */
static unsigned char fallback_image[640 * 1024];
static unsigned char *image_buffer;
static unsigned int image_capacity;
static void release_large(void);
static LinuxMemoryState exec_previous_memory;
static struct {
    ElfPlan plan;
    char strings[STRING_MAX], path[EXEC_PATH_MAX];
    unsigned int offsets[VECTOR_MAX * 2], argc, envc, used;
} staged;
static int active;
static unsigned int pending_entry, pending_stack;

static int stage_string(unsigned int address, int user, unsigned int *offset)
{
    *offset = staged.used;
    for (;;) {
        if (staged.used == STRING_MAX) return -7; /* E2BIG */
        if (user && !linux_user_range(address, 1)) return -14;
        char ch = *(const char *)address++;
        staged.strings[staged.used++] = ch;
        if (!ch) return 0;
    }
}
static int stage_vector(unsigned int address, unsigned int offset, unsigned int *count)
{
    *count = 0;
    if (!address) return 0; /* Linux accepts NULL argv/envp. */
    for (;;) {
        if (!linux_user_range(address, 4)) return -14;
        unsigned int string;
        memcpy(&string, (const void *)address, 4);
        if (!string) return 0;
        if (*count == VECTOR_MAX) return -7;
        int error = stage_string(string, 1, &staged.offsets[offset + *count]);
        if (error) return error;
        (*count)++;
        address += 4;
    }
}
/* No fallible operations below: all lengths, vectors and ELF ranges are
 * validated before the caller disables the old address space. */
static unsigned int install_image(const void *image)
{
    const ElfPlan *plan = &staged.plan;
    unsigned int physical = (unsigned int)linux_memory_physical_base(plan->base);
    unsigned int arena_size = linux_memory_arena_size(plan->base);
    unsigned int delta = plan->base - physical;
    unsigned int sp = physical + arena_size;
    memset((void *)physical, 0, arena_size);
    for (unsigned int i = 0; i < plan->count; i++) {
        const ElfSegment *s = &plan->segments[i];
        memcpy((void *)(physical + s->address - plan->base),
               (const unsigned char *)image + s->offset, s->file_size);
    }
    sp -= staged.used;
    memcpy((void *)sp, staged.strings, staged.used);
    unsigned int strings = sp + delta;
    unsigned int path_size = strlen(staged.path) + 1;
    sp -= path_size;
    memcpy((void *)sp, staged.path, path_size);
    unsigned int execfn = sp + delta;
    unsigned int phdr = plan->phdr;
    if (!phdr) {
        sp = (sp - plan->phnum * 32) & ~3U;
        memcpy((void *)sp, (const unsigned char *)image + plan->phoff, plan->phnum * 32);
        phdr = sp + delta;
    }
    unsigned int uid=permissions_uid();
    unsigned int auxv[] = {
        3, phdr, 4, 32, 5, plan->phnum, 6, 4096, 7, 0, 8, 0,
        9, plan->entry, 11, uid, 12, uid, 13, uid, 14, uid, 23, 0, 31, execfn, 0, 0
    };
    unsigned int words = 1 + staged.argc + 1 + staged.envc + 1 + sizeof(auxv) / sizeof(auxv[0]);
    sp = (sp - words * 4) & ~15U;
    unsigned int *stack = (unsigned int *)sp;
    *stack++ = staged.argc;
    for (unsigned int i = 0; i < staged.argc; i++) *stack++ = strings + staged.offsets[i];
    *stack++ = 0;
    for (unsigned int i = 0; i < staged.envc; i++) *stack++ = strings + staged.offsets[VECTOR_MAX + i];
    *stack++ = 0;
    memcpy(stack, auxv, sizeof(auxv));
    unsigned int image_end = plan->base;
    for (unsigned int i = 0; i < plan->count; i++) {
        unsigned int end = plan->segments[i].address + plan->segments[i].memory_size;
        if (end > image_end) image_end = end;
    }
    linux_memory_enter(plan->base, image_end);
    return sp + delta;
}

int linux_run_image(const void *image, unsigned int size, int argc,
                    const char *const *argv, int *status)
{
    if (active || !status || argc < 0 || argc > VECTOR_MAX || (argc && !argv)) return -22;
    char previous_cwd[4096];
    int error = fs_getcwd(previous_cwd, sizeof(previous_cwd));
    if (error < 0) return error;
    const char *elf_error = elf_validate(image, size, &staged.plan);
    if (elf_error) { print(RED, "Linux ELF: "); println(RED, elf_error); return -8; }
    if (staged.plan.legacy) return -8;
    staged.argc = argc; staged.envc = staged.used = 0; staged.path[0] = 0;
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) return -22;
        error = stage_string((unsigned int)argv[i], 0, &staged.offsets[i]);
        if (error) return error;
    }
    /* GCC and other drivers need PATH when launching their child tools. */
    error = stage_string((unsigned int)"PATH=/usr/bin:/bin", 0,
                         &staged.offsets[VECTOR_MAX]);
    if (error) return error;
    staged.envc = 1;
    if (argc) {
        unsigned int n = strlen(argv[0]);
        if (n >= sizeof(staged.path)) n = sizeof(staged.path) - 1;
        memcpy(staged.path, argv[0], n); staged.path[n] = 0;
    }
    error = linux_memory_prepare(staged.plan.base);
    if (error) return error;
    /* Fork snapshots of the large window come with the first fork (linux_task.c):
     * a program that runs alone (Python) needs only its arena. */
    active = 1; pending_entry = 0;
    linux_syscall_reset();
    linux_task_init();
    linux_tss_init();
    linux_idt_init();
    unsigned int sp = install_image(image);
    *status = linux_enter_user(staged.plan.entry, sp);
    linux_memory_leave();
    linux_idt_restore();
    active = 0;
    release_large();   /* a shell program's memory goes back after it */
    if (fs_cd(previous_cwd)) return -5;
    return 0;
}

/* The exec buffer fits the program (GCC's cc1 is tens of MiB, most are small). */
static int image_kept;
static int image_buffer_fit(unsigned int bytes)
{
    if (image_buffer == fallback_image) { image_buffer = 0; image_capacity = 0; }
    void *block = image_buffer;
    int error = memory_kernel_fit(&block, &image_capacity, bytes);
    image_buffer = block;
    if (!image_buffer) { image_buffer = fallback_image; image_capacity = sizeof(fallback_image); }
    return error;
}
static void image_buffer_release(void)
{
    if (image_kept || !image_buffer || image_buffer == fallback_image) return;
    if (!memory_kernel_free(image_buffer, image_capacity)) { image_buffer = fallback_image; image_capacity = sizeof(fallback_image); }
}
static int read_image(const char *path)
{
    if (!image_buffer) image_buffer_fit(sizeof(fallback_image));
    int size = fs_load_executable(path, image_buffer, image_capacity);
    if (size == -5) {
        char full[256];
        unsigned int n = strlen(path);
        if (n >= sizeof(full)) return size;
        memcpy(full, path, n + 1);
        fs_executable_path(full, sizeof(full));
        int bytes = fs_file_size(full);
        if (bytes > (int)image_capacity && bytes <= (int)FS_MAX_EXECUTABLE_SIZE && !image_buffer_fit(bytes))
            size = fs_load_executable(path, image_buffer, image_capacity);
    }
    return size;
}
/* Newest first: whatever is on top of kernel memory goes back. */
static void release_large(void)
{
    for (int round = 0; round < 3; round++) {
        image_buffer_release();
        linux_task_release_large();
        linux_memory_release();
    }
}
/* Everything GCC needs (exec buffer, the large arena, fork snapshots),
 * taken before a native program such as Happy Gouse gets the rest of RAM:
 * its IDE and Prompt run the compiler later through ABI 13 run(). */
int linux_toolchain_prepare(int with_processes)
{
    /* Python alone needs no exec buffer (no processes); GCC runs cc1 */
    if (with_processes && (image_buffer_fit(FS_MAX_EXECUTABLE_SIZE) || image_buffer == fallback_image)) return -12;
    if (linux_memory_prepare(LINUX_LARGE_BASE)) return -12;
    linux_memory_keep(1);
    if (!with_processes) return 0;
    image_kept = 1;
    linux_task_keep(1);
    return linux_task_prepare_large();
}
/* Gives the reservation back (a full-screen program such as BJVM runs
 * instead of Happy Gouse); Gouse takes it again when it starts. */
void linux_toolchain_release(void)
{
    linux_task_keep(0);
    linux_memory_keep(0);
    image_kept = 0;
    release_large();
}
int linux_run(const char *path, int argc, const char *const *argv, int *status)
{
    if (active) return -16;
    int size = read_image(path);
    if (size < 0) return size;
    return linux_run_image(image_buffer, size, argc, argv, status);
}

int linux_execve(unsigned int path, unsigned int argv, unsigned int envp)
{
    if (!active) return -22;
    for (unsigned int i = 0;; i++, path++) {
        if (i == sizeof(staged.path)) return -36;
        if (!linux_user_range(path, 1)) return -14;
        staged.path[i] = *(const char *)path;
        if (!staged.path[i]) break;
    }
    staged.used = 0;
    int error = stage_vector(argv, 0, &staged.argc);
    if (error) return error;
    error = stage_vector(envp, VECTOR_MAX, &staged.envc);
    if (error) return error;
    /* Linux supplies an empty argv[0] for an empty argument list. */
    if (!staged.argc) {
        error = stage_string((unsigned int)"", 0, &staged.offsets[0]);
        if (error) return error;
        staged.argc = 1;
    }
    /* The executable staging allocation may have a physical address inside
     * the old process's virtual window. Read it with the old mapping disabled. */
    linux_memory_capture(&exec_previous_memory);
    linux_memory_leave();
    fs_executable_path(staged.path, sizeof(staged.path));
    error = fs_exec_check(staged.path);
    if (error) goto restore_old_memory;
    int size = read_image(staged.path);
    if (size < 0) { error = size; goto restore_old_memory; }
    if (elf_validate(image_buffer, size, &staged.plan) || staged.plan.legacy) {
        error = -8;
        goto restore_old_memory;
    }
    error = linux_memory_prepare(staged.plan.base);
    if (error) goto restore_old_memory;
    /* execve in a forked child: other processes are switched with it */
    if (staged.plan.base == LINUX_LARGE_BASE && linux_task_count() > 1 && (error = linux_task_prepare_large()))
        goto restore_old_memory;
    pending_stack = install_image(image_buffer);
    pending_entry = staged.plan.entry;
    linux_tss_tls_set(0);
    linux_syscall_exec();
    return 0;
restore_old_memory:
    linux_memory_resume(&exec_previous_memory);
    return error;
}

void linux_exec_apply(LinuxTrapFrame *frame)
{
    if (!pending_entry) return;
    /* Return from int 0x80 into the new program, never into the old call site.
     * The original linux_enter_user kernel continuation remains untouched. */
    memset(frame, 0, sizeof(*frame));
    frame->ds = frame->es = frame->fs = frame->gs = 0x23;
    frame->eip = pending_entry; frame->cs = 0x1b;
    frame->eflags = 2; frame->user_esp = pending_stack; frame->ss = 0x23;
    pending_entry = 0;
}
