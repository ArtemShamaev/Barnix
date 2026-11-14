#include "linux_task.h"
#include "linux_memory.h"
#include "linux_user.h"
#include "app_abi.h"
#include "barnix.h"
#include "fs.h"
#include "memory.h"
#include "linux_limits.h"
#include "linux_tss.h"

enum { UNUSED, RUNNING, READY, WAITING, IO_WAITING, ZOMBIE };
enum { ARENA_SIZE = APP_STACK_TOP - APP_LOAD_MIN };
typedef struct {
    int state, pid, parent, status, wait_pid;
    unsigned int wait_status;
    LinuxTrapFrame frame;
    LinuxMemoryState memory;
    unsigned long long tls_descriptor;
    unsigned char fx_state[512] __attribute__((aligned(16)));
    char cwd[4096];
} Task;
_Static_assert(LINUX_TASK_LIMIT == 3, "task snapshot arena in linker.ld holds three slots");
static Task tasks[LINUX_TASK_LIMIT];
static void *large_snapshots[LINUX_TASK_LIMIT];
static unsigned int current;
static int next_pid, root_status;
static void exit_task(LinuxTrapFrame *frame, int status);
static void *snapshot(unsigned int slot)
{ return tasks[slot].memory.base == LINUX_LARGE_BASE ? large_snapshots[slot] :
         (void *)(APP_STACK_TOP + slot * ARENA_SIZE); }
static int snapshots_kept;
void linux_task_keep(int keep) { snapshots_kept = keep; }
void linux_task_release_large(void)
{
    if (snapshots_kept) return;
    for (int slot = LINUX_TASK_LIMIT - 1; slot >= 0; slot--) {
        if (!large_snapshots[slot]) continue;
        if (memory_kernel_free(large_snapshots[slot], LINUX_LARGE_ARENA_SIZE)) return;   /* not the newest */
        large_snapshots[slot] = 0;
    }
}
int linux_task_prepare_large(void)
{
    for (unsigned int slot = 0; slot < LINUX_TASK_LIMIT; slot++) {
        if (!large_snapshots[slot])
            large_snapshots[slot] = memory_kernel_alloc(LINUX_LARGE_ARENA_SIZE);
        if (!large_snapshots[slot]) return -12;
    }
    return 0;
}
void linux_task_init(void)
{
    memset(tasks, 0, sizeof(tasks));
    current = 0; next_pid = 2; root_status = 0;
    tasks[0].state = RUNNING; tasks[0].pid = 1; tasks[0].parent = 0;
}
static int matches(const Task *parent, const Task *child, int pid)
{ return child->state && child->parent == parent->pid && (pid <= 0 || child->pid == pid); }
static void save_current(LinuxTrapFrame *frame)
{
    Task *task = &tasks[current];
    task->frame = *frame;
    task->tls_descriptor = linux_tss_tls_get();
    __asm__ volatile("fxsave %0" : "=m"(task->fx_state) : : "memory");
    linux_memory_capture(&task->memory);
    if (fs_getcwd(task->cwd, sizeof(task->cwd)) < 0) panic("cannot save task cwd");
    memcpy(snapshot(current), linux_memory_physical_base(task->memory.base),
           linux_memory_arena_size(task->memory.base));
}
static void wake_waiters(void)
{
    for (unsigned int i = 0; i < LINUX_TASK_LIMIT; i++) {
        Task *parent = &tasks[i];
        if (parent->state != WAITING) continue;
        for (unsigned int j = 0; j < LINUX_TASK_LIMIT; j++) {
            Task *child = &tasks[j];
            if (child->state != ZOMBIE || !matches(parent, child, parent->wait_pid)) continue;
            if (parent->wait_status) {
                unsigned int offset = parent->wait_status - parent->memory.base;
                memcpy((unsigned char *)snapshot(i) + offset, &child->status, 4);
            }
            parent->frame.eax = child->pid;
            parent->state = READY; child->state = UNUSED;
            break;
        }
    }
}
static void schedule(LinuxTrapFrame *frame)
{
    wake_waiters();
    for (unsigned int step = 1; step <= LINUX_TASK_LIMIT; step++) {
        unsigned int slot = (current + step) % LINUX_TASK_LIMIT;
        Task *task = &tasks[slot];
        int io = task->state == IO_WAITING;
        if (task->state != READY && !(io && linux_io_ready(slot))) continue;
        linux_memory_leave();
        memcpy(linux_memory_physical_base(task->memory.base), snapshot(slot),
               linux_memory_arena_size(task->memory.base));
        linux_memory_resume(&task->memory);
        linux_tss_tls_set(task->tls_descriptor);
        __asm__ volatile("fxrstor %0" : : "m"(task->fx_state) : "memory");
        if (fs_cd(task->cwd)) panic("cannot restore task cwd");
        current = slot; task->state = RUNNING;
        linux_files_switch(slot);
        *frame = task->frame;
        if (io) {
            int result = linux_io_resume();
            if (result == LINUX_IO_WAIT) {
                task->state = IO_WAITING;
                /* A large write filled the pipe again. Its kernel-owned
                 * cursor advanced; the saved user address space is intact. */
                schedule(frame);
                return;
            }
            if (result == LINUX_IO_SIGPIPE) { exit_task(frame, 13); return; }
            frame->eax = result;
        }
        return;
    }
    for (unsigned int i = 0; i < LINUX_TASK_LIMIT; i++) {
        if (tasks[i].state == IO_WAITING || tasks[i].state == WAITING) {
            println(RED, "Linux processes blocked with no runnable peer");
            root_status = 125;
            break;
        }
    }
    /* No runnable processes remain. The synchronous launch returns to Bssh. */
    for (unsigned int i = 0; i < LINUX_TASK_LIMIT; i++) linux_files_exit(i);
    linux_leave_user(root_status);
}
static void exit_task(LinuxTrapFrame *frame, int status)
{
    Task *task = &tasks[current];
    task->status = status; task->state = ZOMBIE;
    if (!current) root_status = status & 127 ? 128 + (status & 127) : status >> 8;
    /* Reparent grandchildren to this launch's init process while it lives. */
    for (unsigned int i = 1; i < LINUX_TASK_LIMIT; i++)
        if (tasks[i].state && tasks[i].parent == task->pid)
            tasks[i].parent = tasks[0].state != ZOMBIE ? 1 : 0;
    linux_files_exit(current);
    schedule(frame);
}
int linux_task_count(void)
{
    int n = 0;
    for (unsigned int i = 0; i < LINUX_TASK_LIMIT; i++) if (tasks[i].state) n++;
    return n;
}
static int fork_task(LinuxTrapFrame *frame)
{
    unsigned int child;
    /* A process of the large window keeps a copy while the other runs. */
    if (linux_memory_current_base() == LINUX_LARGE_BASE && linux_task_prepare_large()) return -12;
    for (child = 0; child < LINUX_TASK_LIMIT; child++) if (!tasks[child].state) break;
    if (child == LINUX_TASK_LIMIT) return -11;
    if (next_pid == 0x7fffffff) return -11;
    unsigned int parent = current;
    int pid = next_pid++;
    frame->eax = pid;
    save_current(frame);
    tasks[parent].state = READY;
    Task *task = &tasks[child];
    memset(task, 0, sizeof(*task));
    task->state = RUNNING; task->pid = pid; task->parent = tasks[parent].pid;
    task->tls_descriptor = linux_tss_tls_get();
    linux_files_fork(parent, child);
    current = child; linux_files_switch(child);
    /* Child starts first, using the current copy. Parent has its own snapshot. */
    frame->eax = 0;
    return 0;
}
static int wait_task(LinuxTrapFrame *frame)
{
    int pid = frame->ebx;
    if (frame->edx & ~1U) return -22; /* WNOHANG only, no stopped processes. */
    if (pid < -1) return -38; /* Process-group waits are not implemented. */
    if (frame->ecx && !linux_user_range(frame->ecx, 4)) return -14;
    Task *parent = &tasks[current];
    int children = 0;
    for (unsigned int i = 0; i < LINUX_TASK_LIMIT; i++) {
        Task *child = &tasks[i];
        if (!matches(parent, child, pid)) continue;
        children = 1;
        if (child->state != ZOMBIE) continue;
        if (frame->ecx) memcpy((void *)frame->ecx, &child->status, 4);
        int result = child->pid; child->state = UNUSED;
        return result;
    }
    if (!children) return -10;
    if (frame->edx & 1) return 0;
    parent->wait_pid = pid; parent->wait_status = frame->ecx;
    save_current(frame); parent->state = WAITING;
    schedule(frame);
    return (int)frame->eax;
}
int linux_task_syscall(LinuxTrapFrame *frame)
{
    switch (frame->eax) {
    case __NR_fork: {
        int error = fork_task(frame);
        if (error) frame->eax = error;
        return 1;
    }
    case 120: { /* clone: glibc posix_spawn uses CLONE_VM|CLONE_VFORK|SIGCHLD. */
        if (frame->ebx != 0x4111U || !linux_user_range(frame->ecx - 4U, 4)) {
            frame->eax = -22; return 1;
        }
        unsigned int child_stack = frame->ecx;
        int error = fork_task(frame);
        if (error) frame->eax = error;
        else frame->user_esp = child_stack;
        return 1;
    }
    case __NR_waitpid: frame->eax = wait_task(frame); return 1;
    case 114: /* wait4, with unused rusage pointer in the common GCC path. */
        if (frame->esi) { frame->eax = -38; return 1; }
        frame->eax = wait_task(frame); return 1;
    case __NR_exit:
    case __NR_exit_group: exit_task(frame, (frame->ebx & 255) << 8); return 1;
    case __NR_getpid: frame->eax = tasks[current].pid; return 1;
    case __NR_getppid: frame->eax = tasks[current].parent; return 1;
    case __NR_sched_yield:
        frame->eax = 0; save_current(frame); tasks[current].state = READY;
        schedule(frame); return 1;
    default: return 0;
    }
}
void linux_task_fault_from_regs(LinuxTrapFrame *frame)
{
    exit_task(frame, 11); /* User faults currently map to SIGSEGV. */
}
void linux_task_io_result(LinuxTrapFrame *frame, int result)
{
    if (result == LINUX_IO_SIGPIPE) { exit_task(frame, 13); return; }
    save_current(frame);
    tasks[current].state = IO_WAITING;
    schedule(frame);
}
