#include "linux_exec.h"
#include "linux_user.h"
#include "linux_task.h"
#include "linux_idt.h"
#include "linux_tss.h"
#include "linux_memory.h"
#include "elf_format.h"
#include "barnix.h"
#include "fs.h"

/* Staging is kernel-owned: exec must finish reading every user pointer before
 * replacing the address space. Cooperative tasks share this staging area. */
enum { VECTOR_MAX = 64, STRING_MAX = 8192, EXEC_PATH_MAX = 128 };
static unsigned char image_buffer[FS_MAX_FILE_SIZE];
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
    unsigned int delta = plan->base - APP_LOAD_MIN;
    unsigned int sp = APP_STACK_TOP;
    memset((void *)APP_LOAD_MIN, 0, APP_STACK_TOP - APP_LOAD_MIN);
    for (unsigned int i = 0; i < plan->count; i++) {
        const ElfSegment *s = &plan->segments[i];
        memcpy((void *)(s->address - delta), (const unsigned char *)image + s->offset, s->file_size);
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
    unsigned int auxv[] = {
        3, phdr, 4, 32, 5, plan->phnum, 6, 4096, 7, 0, 8, 0,
        9, plan->entry, 11, 0, 12, 0, 13, 0, 14, 0, 23, 0, 31, execfn, 0, 0
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
    if (argc) {
        unsigned int n = strlen(argv[0]);
        if (n >= sizeof(staged.path)) n = sizeof(staged.path) - 1;
        memcpy(staged.path, argv[0], n); staged.path[n] = 0;
    }
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
    if (fs_cd(previous_cwd)) return -5;
    return 0;
}

static int read_image(const char *path)
{
    int size = fs_size(path);
    if (size < 0) return -2;
    if (size > (int)sizeof(image_buffer)) return -27;
    if (fs_read(path, 0, image_buffer, size) != size) return -5;
    return size;
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
    int error = fs_exec_check(staged.path);
    if (error) return error;
    int size = read_image(staged.path);
    if (size < 0) return size;
    if (elf_validate(image_buffer, size, &staged.plan) || staged.plan.legacy) return -8;
    staged.used = 0;
    error = stage_vector(argv, 0, &staged.argc);
    if (error) return error;
    error = stage_vector(envp, VECTOR_MAX, &staged.envc);
    if (error) return error;
    /* Linux supplies an empty argv[0] for an empty argument list. */
    if (!staged.argc) {
        error = stage_string((unsigned int)"", 0, &staged.offsets[0]);
        if (error) return error;
        staged.argc = 1;
    }
    linux_memory_leave();
    pending_stack = install_image(image_buffer);
    pending_entry = staged.plan.entry;
    linux_syscall_exec();
    return 0;
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
