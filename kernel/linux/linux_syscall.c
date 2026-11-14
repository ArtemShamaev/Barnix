#include <stdint.h>
#include "linux_abi.h"
#include "linux_exec.h"
#include "linux_task.h"
#include "linux_user.h"
#include "linux_memory.h"
#include "linux_tss.h"
#include "fs.h"
#include "permissions.h"
#include "barnix.h"
#include "keyboard.h"
#include "elf.h"
void net_random(void *out, unsigned int length);

/* A deliberately bounded, synchronous Linux i386 ABI. Unsupported operations
 * fail with ENOSYS; they must not report success without doing the work. */
enum { FD_COUNT = 16, PATH_SIZE = 512, EFAULT = 14, ENOENT = 2,
       EIO = 5, EACCES = 13, EMFILE = 24, ENOTTY = 25, ESPIPE = 29,
       EISDIR = 21, ENAMETOOLONG = 36, EFBIG = 27, EAGAIN = 11, ENFILE = 23 };
enum { O_ACCMODE = 3, O_WRONLY = 1, O_RDWR = 2, O_CREAT = 64,
       O_EXCL = 128, O_NOCTTY = 256, O_TRUNC = 512, O_APPEND = 1024, O_NONBLOCK = 2048,
       O_LARGEFILE = 32768, O_DIRECTORY = 65536, O_NOFOLLOW = 0400000, O_CLOEXEC = 02000000 };
enum { PIPE_SIZE = 4096, PIPE_COUNT = FD_COUNT * LINUX_TASK_LIMIT / 2, IOV_MAX = 1024 };
typedef struct {
    unsigned int readers, writers, head, used;
    unsigned char data[PIPE_SIZE];
} Pipe;
typedef struct {
    unsigned int refs, position, flags;
    int terminal, directory;
    Pipe *pipe;
    char name[PATH_SIZE];
} OpenFile;
typedef struct { unsigned int address, length; } IoVector;
/* Linux i386 stat64 layout: the 32-bit inode at 12 coexists with st_ino at 88. */
typedef struct __attribute__((packed)) {
    unsigned long long device;
    unsigned int pad1, inode_legacy, mode, links, uid, gid;
    unsigned long long rdevice;
    unsigned int pad2;
    unsigned long long size;
    unsigned int block_size;
    unsigned long long blocks;
    unsigned int atime, atime_nsec, mtime, mtime_nsec, ctime, ctime_nsec;
    unsigned long long inode;
} LinuxStat64;
_Static_assert(sizeof(LinuxStat64) == 96 &&
               __builtin_offsetof(LinuxStat64, size) == 44 &&
               __builtin_offsetof(LinuxStat64, inode) == 88, "Linux i386 stat64 layout");
typedef struct {
    int active, writing;
    unsigned int fd, count, index, offset, total, length;
    IoVector vectors[IOV_MAX];
} PipeIo;
static Pipe pipes[PIPE_COUNT];
static PipeIo pending_io[LINUX_TASK_LIMIT];
static OpenFile descriptions[FD_COUNT * LINUX_TASK_LIMIT];
static OpenFile *task_files[LINUX_TASK_LIMIT][FD_COUNT];
static unsigned int task_flags[LINUX_TASK_LIMIT][FD_COUNT];
static unsigned int file_task;
static unsigned int clock_calls;
static unsigned char signal_actions[LINUX_TASK_LIMIT][65][20];
static unsigned char signal_masks[LINUX_TASK_LIMIT][8];
static unsigned int task_umask[LINUX_TASK_LIMIT];
#define files task_files[file_task]
#define descriptor_flags task_flags[file_task]


void linux_syscall_reset(void)
{
    clock_calls = 0;
    memset(signal_actions, 0, sizeof(signal_actions));
    memset(signal_masks, 0, sizeof(signal_masks));
    for (unsigned int slot = 0; slot < LINUX_TASK_LIMIT; slot++) task_umask[slot] = 022;
    memset(descriptions, 0, sizeof(descriptions));
    memset(pipes, 0, sizeof(pipes));
    memset(pending_io, 0, sizeof(pending_io));
    file_task = 0;
    memset(task_files, 0, sizeof(task_files));
    memset(task_flags, 0, sizeof(task_flags));
    for (int i = 0; i < 3; i++) {
        files[i] = &descriptions[i];
        files[i]->refs = 1;
        files[i]->terminal = 1;
        files[i]->flags = i ? O_WRONLY : 0;
    }
}
static OpenFile *get_file(unsigned int fd)
{ return fd < FD_COUNT ? files[fd] : 0; }
static int free_fd(unsigned int minimum)
{
    for (unsigned int i = minimum; i < FD_COUNT; i++) if (!files[i]) return i;
    return -EMFILE;
}
static int close_fd(unsigned int fd)
{
    OpenFile *file = get_file(fd);
    if (!file) return -LINUX_EBADF;
    if (!--file->refs && file->pipe) {
        if (file->flags & O_ACCMODE) file->pipe->writers--;
        else file->pipe->readers--;
    }
    files[fd] = 0; descriptor_flags[fd] = 0;
    return 0;
}
void linux_syscall_exec(void)
{
    for (unsigned int fd = 0; fd < FD_COUNT; fd++)
        if (files[fd] && (descriptor_flags[fd] & 1)) close_fd(fd);
    memset(signal_actions[file_task], 0, sizeof(signal_actions[file_task]));
}
void linux_files_switch(unsigned int slot) { file_task = slot; }
void linux_files_fork(unsigned int parent, unsigned int child)
{
    pending_io[child].active = 0;
    memcpy(signal_actions[child], signal_actions[parent], sizeof(signal_actions[child]));
    memcpy(signal_masks[child], signal_masks[parent], sizeof(signal_masks[child]));
    task_umask[child] = task_umask[parent];
    for (unsigned int fd = 0; fd < FD_COUNT; fd++) {
        task_files[child][fd] = task_files[parent][fd];
        task_flags[child][fd] = task_flags[parent][fd];
        if (task_files[child][fd]) task_files[child][fd]->refs++;
    }
}
void linux_files_exit(unsigned int slot)
{
    pending_io[slot].active = 0;
    unsigned int previous = file_task; file_task = slot;
    for (unsigned int fd = 0; fd < FD_COUNT; fd++) if (files[fd]) close_fd(fd);
    file_task = previous;
}
static int create_pipe(unsigned int address, unsigned int flags)
{
    if (flags & ~(O_NONBLOCK | O_CLOEXEC)) return -LINUX_EINVAL;
    if (!linux_user_range(address, 8)) return -EFAULT;
    int fds[2], found = 0;
    for (int i = 0; i < FD_COUNT && found < 2; i++)
        if (!files[i]) fds[found++] = i;
    if (found != 2) return -EMFILE;
    OpenFile *ends[2]; found = 0;
    for (unsigned int i = 0; i < FD_COUNT * LINUX_TASK_LIMIT && found < 2; i++)
        if (!descriptions[i].refs) ends[found++] = &descriptions[i];
    if (found != 2) return -ENFILE;
    Pipe *pipe = 0;
    for (unsigned int i = 0; i < PIPE_COUNT; i++)
        if (!pipes[i].readers && !pipes[i].writers) { pipe = &pipes[i]; break; }
    if (!pipe) return -ENFILE;
    /* Nothing is published until both descriptors and the buffer are ready. */
    pipe->head = pipe->used = 0;
    pipe->readers = pipe->writers = 1;
    for (unsigned int i = 0; i < 2; i++) {
        memset(ends[i], 0, sizeof(*ends[i]));
        ends[i]->refs = 1; ends[i]->pipe = pipe;
        ends[i]->flags = (i ? O_WRONLY : 0) | (flags & O_NONBLOCK);
        files[fds[i]] = ends[i];
        descriptor_flags[fds[i]] = (flags & O_CLOEXEC) ? 1 : 0;
    }
    memcpy((void *)(uintptr_t)address, fds, sizeof(fds));
    return 0;
}
int linux_io_ready(unsigned int slot)
{
    PipeIo *io = &pending_io[slot];
    if (!io->active) return 0;
    OpenFile *file = task_files[slot][io->fd];
    Pipe *pipe = file->pipe;
    if (file->flags & O_NONBLOCK) return 1;
    if (!io->writing) return pipe->used || !pipe->writers;
    unsigned int need = io->length <= PIPE_SIZE ? io->length : 1;
    return !pipe->readers || PIPE_SIZE - pipe->used >= need;
}
int linux_io_resume(void)
{
    PipeIo *io = &pending_io[file_task];
    OpenFile *file = get_file(io->fd);
    Pipe *pipe = file->pipe;
    int result;
    if (io->writing && !pipe->readers) {
        /* Signal handlers/masks are not implemented: use SIGPIPE's default
         * action, including when the reader disappears during a large write. */
        result = LINUX_IO_SIGPIPE;
        goto done;
    }
    if (!io->writing && !pipe->used && !pipe->writers) { result = 0; goto done; }
    unsigned int available = io->writing ? PIPE_SIZE - pipe->used : pipe->used;
    if (!available || (io->writing && io->length <= PIPE_SIZE && available < io->length)) {
        if (!(file->flags & O_NONBLOCK)) return LINUX_IO_WAIT;
        result = io->total ? (int)io->total : -EAGAIN;
        goto done;
    }
    unsigned int remaining = io->length - io->total;
    if (available > remaining) available = remaining;
    while (available) {
        IoVector *vector = &io->vectors[io->index];
        if (io->offset == vector->length) { io->index++; io->offset = 0; continue; }
        unsigned int position = io->writing ? (pipe->head + pipe->used) % PIPE_SIZE : pipe->head;
        unsigned int chunk = vector->length - io->offset;
        if (chunk > available) chunk = available;
        if (chunk > PIPE_SIZE - position) chunk = PIPE_SIZE - position;
        void *user = (void *)(uintptr_t)(vector->address + io->offset);
        if (io->writing) {
            memcpy(pipe->data + position, user, chunk);
            pipe->used += chunk;
        } else {
            memcpy(user, pipe->data + position, chunk);
            pipe->used -= chunk;
            pipe->head = (position + chunk) % PIPE_SIZE;
        }
        available -= chunk; io->offset += chunk; io->total += chunk;
    }
    if (io->writing && io->total < io->length && !(file->flags & O_NONBLOCK))
        return LINUX_IO_WAIT;
    result = io->total;
done:
    io->active = 0;
    return result;
}
static int pipe_transfer(unsigned int fd, unsigned int address, unsigned int count,
                         int writing, int vectored)
{
    OpenFile *file = get_file(fd);
    if (writing ? !(file->flags & O_ACCMODE) : (file->flags & O_ACCMODE) == O_WRONLY)
        return -LINUX_EBADF;
    PipeIo *io = &pending_io[file_task];
    io->active = 0;
    io->fd = fd; io->writing = writing;
    io->count = vectored ? count : 1;
    io->index = io->offset = io->total = io->length = 0;
    if (vectored) {
        if (count > IOV_MAX) return -LINUX_EINVAL;
        if (count && !linux_user_range(address, count * sizeof(IoVector))) return -EFAULT;
        if (count) memcpy(io->vectors, (const void *)(uintptr_t)address, count * sizeof(IoVector));
    } else io->vectors[0] = (IoVector){address, count};
    for (unsigned int i = 0; i < io->count; i++) {
        IoVector *v = &io->vectors[i];
        if (v->length > 0x7fffffffU - io->length) return -LINUX_EINVAL;
        if (v->length && !linux_user_range(v->address, v->length)) return -EFAULT;
        io->length += v->length;
    }
    if (!io->length) return 0;
    io->active = 1;
    return linux_io_resume();
}
static int copy_path(char *out, unsigned int address)
{
    for (unsigned int i = 0; i < PATH_SIZE; i++) {
        if (!linux_user_range(address, 1)) return -EFAULT;
        out[i] = *(const char *)(uintptr_t)address++;
        if (!out[i]) return i ? 0 : -ENOENT;
    }
    return -ENAMETOOLONG;
}
static int copy_stat64(unsigned int address, const FsFileInfo *info)
{
    if (!linux_user_range(address, sizeof(LinuxStat64))) return -EFAULT;
    LinuxStat64 stat;
    memset(&stat, 0, sizeof(stat));
    stat.device = 1;
    stat.inode_legacy = info->inode;
    stat.inode = info->inode;
    stat.mode = info->mode;
    stat.links = info->links;
    stat.uid = info->uid;
    stat.gid = info->gid;
    stat.size = info->size;
    stat.block_size = 1024;
    stat.blocks = info->blocks;
    stat.atime = info->atime;
    stat.mtime = info->mtime;
    stat.ctime = info->ctime;
    memcpy((void *)(uintptr_t)address, &stat, sizeof(stat));
    return 0;
}
static int transfer(unsigned int fd, unsigned int address, unsigned int count, int writing)
{
    OpenFile *file = get_file(fd);
    if (!file || (writing ? (file->flags & O_ACCMODE) == 0 :
                            (file->flags & O_ACCMODE) == O_WRONLY)) return -LINUX_EBADF;
    if (file->directory) return -EISDIR;
    if (file->pipe) return pipe_transfer(fd, address, count, writing, 0);
    if (!count) return 0;
    if (!linux_user_range(address, count)) return -EFAULT;
    unsigned char *data = (unsigned char *)(uintptr_t)address;
    if (file->terminal) {
        if (!writing) {
            /* A terminal reads whole lines, shown as they are typed
             * (backspace, Ctrl+V paste), like a Linux tty in canonical mode. */
            static char line[258];
            static unsigned int line_length, line_at;
            if (line_at >= line_length) {
                elf_nested_input();
                input(line, 256, "");
                console_capture_add(line); console_capture_add("\n");   /* the transcript shows what was typed */
                line_length = (unsigned int)strlen(line);
                line[line_length++] = '\n';
                line_at = 0;
            }
            unsigned int n = 0;
            while (n < count && line_at < line_length) data[n++] = (unsigned char)line[line_at++];
            return (int)n;
        }
        for (unsigned int i = 0; i < count; i++) __asm__ volatile("outb %0, %1" : : "a"(data[i]), "Nd"((unsigned short)0xe9));
        console_ansi_write((const char *)data, count);
        return (int)count;
    }
    if (!writing) {
        int got = fs_read(file->name, file->position, data, count);
        if (got < 0) return -EIO;
        file->position += got;
        return got;
    }
    int end=fs_pwrite(file->name,file->position,data,count,file->flags & O_APPEND);
    if(end<0)return end;
    file->position=end;
    return count;
}
int linux_syscall_dispatch(LinuxSyscallFrame *frame)
{
    if (!frame) return -LINUX_EINVAL;
    unsigned int a = frame->ebx, b = frame->ecx, c = frame->edx;
    switch (frame->eax) {
    case LINUX_SYS_exit:
    case LINUX_SYS_exit_group: /* exit_group: there is only one thread. */
        linux_leave_user(a & 255);
    case LINUX_SYS_execve: return linux_execve(a, b, c);
    case LINUX_SYS_brk: return linux_memory_brk(a);
    case 125: return linux_memory_protect(a, b, c); /* mprotect */
    case __NR_mmap2: {
        unsigned int flags = frame->esi, fd = frame->edi, page_offset = frame->ebp;
        if (flags & 16U || (flags & 3U) != 2U ||
            (flags & ~(1U | 2U | 16U | 32U | 0x20000U))) return -LINUX_EINVAL;
        OpenFile *file = 0;
        if (!(flags & 32U)) {
            file = get_file(fd);
            if (!file) return -LINUX_EBADF;
            if (file->directory || file->terminal || file->pipe ||
                (file->flags & O_ACCMODE) == O_WRONLY) return -LINUX_EINVAL;
            if (page_offset > 0x7ffffU) return -LINUX_EINVAL;
        }
        int mapped = linux_memory_map(b, 3);
        if ((unsigned int)mapped >= 0xfffff000U) return mapped;
        if (file) {
            int got = fs_read(file->name, page_offset * 4096U, (void *)(uintptr_t)(unsigned int)mapped, b);
            if (got < 0) { linux_memory_unmap((unsigned int)mapped, b); return -EIO; }
        }
        if (!(c & 2U)) linux_memory_protect((unsigned int)mapped, b, c | 1U);
        return mapped;
    }
    case __NR_munmap: return linux_memory_unmap(a, b);
    case __NR_clock_gettime:
    case 403: { /* clock_gettime64: i386 uses two 64-bit fields. */
        if (a > 11 || !linux_user_range(b, frame->eax == 403 ? 16 : 8)) return -LINUX_EINVAL;
        unsigned int tick = clock_calls++;
        unsigned int sec = (a == 0 || a == 5 || a == 8) ? 1790000000U + tick / 1000 : tick / 1000;
        unsigned int nsec = tick % 1000 * 1000000U;
        if (frame->eax == 403) {
            unsigned int result[4] = {sec, 0, nsec, 0};
            memcpy((void *)(uintptr_t)b, result, sizeof(result));
        } else {
            unsigned int result[2] = {sec, nsec};
            memcpy((void *)(uintptr_t)b, result, sizeof(result));
        }
        return 0;
    }
    case __NR_gettimeofday: {
        if (a) {
            if (!linux_user_range(a, 8)) return -EFAULT;
            unsigned int result[2] = {1790000000U + clock_calls / 1000, clock_calls % 1000 * 1000U};
            memcpy((void *)(uintptr_t)a, result, sizeof(result));
            clock_calls++;
        }
        if (b) { if (!linux_user_range(b, 8)) return -EFAULT; memset((void *)(uintptr_t)b, 0, 8); }
        return 0;
    }
    case __NR_nanosleep: {
        if (!linux_user_range(a, 8)) return -EFAULT;
        const unsigned int *request = (const unsigned int *)(uintptr_t)a;
        if (request[1] >= 1000000000U) return -LINUX_EINVAL;
        clock_calls += request[0] * 1000U + request[1] / 1000000U + 1U;
        if (b) { if (!linux_user_range(b, 8)) return -EFAULT; memset((void *)(uintptr_t)b, 0, 8); }
        return 0;
    }
    case 240:
    case 422: { /* futex and futex_time64; no CLONE_THREAD support yet. */
        if ((a & 3U) || !linux_user_range(a, 4)) return -EFAULT;
        unsigned int operation = b & ~128U; /* FUTEX_PRIVATE_FLAG */
        if (operation == 1 || operation == 10) return 0; /* WAKE, WAKE_BITSET */
        if (operation == 0 || operation == 9) return -EAGAIN; /* no peer can wake us */
        return -LINUX_ENOSYS;
    }
    case __NR_rt_sigaction: {
        if (a == 0 || a > 64 || frame->esi != 8) return -LINUX_EINVAL;
        if (c) {
            if (!linux_user_range(c, 20)) return -EFAULT;
            memcpy((void *)(uintptr_t)c, signal_actions[file_task][a], 20);
        }
        if (b) {
            if (!linux_user_range(b, 20)) return -EFAULT;
            if (a == 9 || a == 19) return -LINUX_EINVAL;
            memcpy(signal_actions[file_task][a], (const void *)(uintptr_t)b, 20);
        }
        return 0;
    }
    case __NR_rt_sigprocmask: {
        if (frame->esi != 8) return -LINUX_EINVAL;
        if (c) {
            if (!linux_user_range(c, 8)) return -EFAULT;
            memcpy((void *)(uintptr_t)c, signal_masks[file_task], 8);
        }
        if (b) {
            if (!linux_user_range(b, 8)) return -EFAULT;
            if (a > 2) return -LINUX_EINVAL;
            const unsigned char *set = (const unsigned char *)(uintptr_t)b;
            for (unsigned int i = 0; i < 8; i++) {
                if (a == 0) signal_masks[file_task][i] |= set[i];
                else if (a == 1) signal_masks[file_task][i] &= ~set[i];
                else signal_masks[file_task][i] = set[i];
            }
            signal_masks[file_task][1] &= ~0x01U; /* SIGKILL cannot be masked. */
            signal_masks[file_task][2] &= ~0x04U; /* SIGSTOP cannot be masked. */
        }
        return 0;
    }
    case __NR_set_thread_area: return linux_tss_set_thread_area(a);
    case __NR_set_tid_address: return 1;
    case 224:   /* gettid: one thread per process */
    case LINUX_SYS_getpid:
    case LINUX_SYS_getpgrp: return 1;
    case LINUX_SYS_getppid: return 0;
    case LINUX_SYS_getuid:
    case LINUX_SYS_geteuid:
    case LINUX_SYS_getgid: /* getgid */
    case LINUX_SYS_getegid: /* getegid */
    case LINUX_SYS_getuid32: case LINUX_SYS_getgid32:
    case LINUX_SYS_geteuid32: case LINUX_SYS_getegid32: /* 32-bit IDs */
        return permissions_uid();
    case LINUX_SYS_read: return transfer(a, b, c, 0);
    case LINUX_SYS_write: return transfer(a, b, c, 1);
    case __NR_pipe: return create_pipe(a, 0);
    case __NR_pipe2: return create_pipe(a, b);
    case LINUX_SYS_close: return close_fd(a);
    case 60: { /* umask */
        unsigned int old = task_umask[file_task];
        task_umask[file_task] = a & 0777U;
        return old;
    }
    case 15: { /* chmod */
        char path[PATH_SIZE]; int error = copy_path(path, a);
        return error ? error : fs_chmod(path, b) ? -EACCES : 0;
    }
    case 94: { /* fchmod */
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (file->terminal || file->pipe) return -LINUX_EINVAL;
        return fs_chmod(file->name, b) ? -EACCES : 0;
    }
    case 306: { /* fchmodat */
        if (a != (unsigned int)-100 || frame->esi) return -LINUX_EINVAL;
        char path[PATH_SIZE]; int error = copy_path(path, b);
        return error ? error : fs_chmod(path, c) ? -EACCES : 0;
    }
    case 10:
    case 301: { /* unlink, unlinkat */
        if (frame->eax == 301) {
            if (a != (unsigned int)-100 || c) return -LINUX_EINVAL;
            a = b;
        }
        char path[PATH_SIZE]; int error = copy_path(path, a);
        return error ? error : fs_rm(path) ? -ENOENT : 0;
    }
    case 38: { /* rename */
        char old_path[PATH_SIZE], new_path[PATH_SIZE];
        int error = copy_path(old_path, a);
        if (error) return error;
        error = copy_path(new_path, b);
        return error ? error : fs_mv(old_path, new_path) ? -EIO : 0;
    }
    case LINUX_SYS_open:
    case 295: { /* openat */
        unsigned int create_mode = frame->eax == 295 ? frame->esi : c;
        if (frame->eax == 295) {
            if (a != (unsigned int)-100) return -LINUX_EBADF;
            a = b; b = c;
        }
        char path[PATH_SIZE];
        int error = copy_path(path, a);
        if (error) return error;
        if (path[0] != '/') {
            char cwd[PATH_SIZE];
            int length = fs_getcwd(cwd, sizeof(cwd));
            if (length < 0) return length;
            unsigned int prefix = (unsigned int)length - 1;
            if (prefix > 1) cwd[prefix++] = '/';
            if (prefix + (unsigned int)strlen(path) >= sizeof(cwd)) return -ENAMETOOLONG;
            strcpy(cwd + prefix, path); strcpy(path, cwd);
        }
        if ((b & O_ACCMODE) == 3) return -LINUX_EINVAL;
        if (b & ~(O_ACCMODE | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | O_APPEND | O_NONBLOCK | O_LARGEFILE | O_DIRECTORY |
                  O_NOFOLLOW | O_CLOEXEC))   /* no symbolic links: O_NOFOLLOW holds anyway */
            return -LINUX_EINVAL;
        int fd = free_fd(0);
        if (fd < 0) return fd;
        OpenFile *file = 0;
        for (int i = 0; i < FD_COUNT * LINUX_TASK_LIMIT; i++) if (!descriptions[i].refs) { file = &descriptions[i]; break; }
        if (!file) return -EMFILE;
        if (((b & O_ACCMODE) != 1 && !check_permission(path,'r')) ||
            ((b & O_ACCMODE) != 0 && !check_permission(path,'w'))) return -13;
        int directory = fs_is_dir(path);
        if (directory && ((b & O_ACCMODE) || (b & (O_CREAT | O_TRUNC)))) return -EISDIR;
        int size = directory ? 0 : fs_file_size(path);
        if ((b & O_DIRECTORY) && !directory) return size < 0 ? -ENOENT : -20;
        if (size >= 0 && (b & O_CREAT) && (b & O_EXCL)) return -17;
        if (size < 0) {
            if (!(b & O_CREAT)) return -ENOENT;
            if (fs_write(path, "", 0)) return -EIO;
            if (fs_chmod(path, (create_mode & 0777U) & ~task_umask[file_task])) return -EIO;
        }
        if ((b & O_TRUNC) && (b & O_ACCMODE) && fs_write(path, "", 0)) return -EIO;
        memset(file, 0, sizeof(*file));
        file->refs = 1; file->flags = b & (O_ACCMODE | O_APPEND | O_NONBLOCK | O_LARGEFILE | O_DIRECTORY);
        file->directory = directory;
        strcpy(file->name, path);
        files[fd] = file;
        descriptor_flags[fd] = (b & O_CLOEXEC) ? 1 : 0;
        return fd;
    }
    case LINUX_SYS_dup:
    case LINUX_SYS_dup2: {
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        int fd = frame->eax == LINUX_SYS_dup ? free_fd(0) : (b < FD_COUNT ? (int)b : -LINUX_EBADF);
        if (fd < 0) return fd;
        if ((unsigned int)fd == a) return fd;
        if (files[fd]) close_fd(fd);
        files[fd] = file; file->refs++; descriptor_flags[fd] = 0;
        return fd;
    }
    case LINUX_SYS_fcntl:
    case 221: { /* fcntl64 */
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (b == 0) { /* F_DUPFD */
            if (c >= FD_COUNT) return -LINUX_EINVAL;
            int fd = free_fd(c);
            if (fd < 0) return fd;
            files[fd] = file; file->refs++; descriptor_flags[fd] = 0; return fd;
        }
        if (b == 1) return descriptor_flags[a];
        if (b == 2) { descriptor_flags[a] = c & 1; return 0; }
        if (b == 3) return file->flags;
        if (b == 4) {
            if (c & ~(O_ACCMODE | O_APPEND | O_NONBLOCK | O_LARGEFILE | O_DIRECTORY)) return -LINUX_EINVAL;
            if (file->terminal && (c & O_NONBLOCK)) return -LINUX_EINVAL;
            file->flags = (file->flags & (O_ACCMODE | O_LARGEFILE | O_DIRECTORY)) | (c & (O_APPEND | O_NONBLOCK));
            return 0;
        }
        if (b == 1032 && file->pipe) return PIPE_SIZE; /* F_GETPIPE_SZ */
        return -LINUX_EINVAL;
    }
    case LINUX_SYS_lseek: {
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (file->terminal || file->pipe) return -ESPIPE;
        if (c > 2) return -LINUX_EINVAL;
        int size = fs_file_size(file->name);
        if (size < 0) return -EIO;
        unsigned int base = c == 2 ? (unsigned int)size : c == 1 ? file->position : 0;
        long long position = (long long)base + (int)b;
        if (position < 0) return -LINUX_EINVAL;
        if (position > 0x7fffffff) return -75; /* EOVERFLOW */
        file->position = (unsigned int)position;
        return (int)position;
    }
    case 140: { /* _llseek: 64-bit result through a user pointer. */
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (file->terminal || file->pipe) return -ESPIPE;
        if (!linux_user_range(frame->esi, 8)) return -EFAULT;
        if (frame->edi > 2) return -LINUX_EINVAL;
        int size = fs_file_size(file->name);
        if (size < 0) return -EIO;
        long long offset = (long long)(int)b * 0x100000000LL + c;
        long long base = frame->edi == 2 ? (unsigned int)size : frame->edi == 1 ? file->position : 0U;
        long long position = base + offset;
        if (position < 0 || position > 0x7fffffffLL) return -LINUX_EINVAL;
        file->position = (unsigned int)position;
        unsigned long long result = (unsigned long long)position;
        memcpy((void *)(uintptr_t)frame->esi, &result, sizeof(result));
        return 0;
    }
    case LINUX_SYS_stat64:
    case LINUX_SYS_lstat64: {
        char path[PATH_SIZE]; FsFileInfo info;
        int error = copy_path(path, a);
        if (error) return error;
        error = fs_file_info(path, &info);
        return error ? (error == -EACCES ? error : -ENOENT) : copy_stat64(b, &info);
    }
    case 300: { /* fstatat64 */
        if (frame->esi & ~0x1100U) return -LINUX_EINVAL;
        if (a != (unsigned int)-100 && a >= FD_COUNT) return -LINUX_EBADF;
        char path[PATH_SIZE]; FsFileInfo info;
        if (!linux_user_range(b, 1)) return -EFAULT;
        if (!*(const char *)(uintptr_t)b && (frame->esi & 0x1000U)) {
            OpenFile *file = get_file(a);
            if (!file) return -LINUX_EBADF;
            if (file->terminal || file->pipe) {   /* fstat() of glibc: as fstat64 */
                memset(&info, 0, sizeof(info));
                info.links = 1;
                if (file->terminal) info.mode = 0020666;
                else { info.mode = 0010600; info.size = file->pipe->used; }
                return copy_stat64(c, &info);
            }
            strcpy(path, file->name);
        } else {
            int error = copy_path(path, b);
            if (error) return error;
            if (path[0] != '/' && a != (unsigned int)-100) return -LINUX_EINVAL;
        }
        int error = fs_file_info(path, &info);
        return error ? -ENOENT : copy_stat64(c, &info);
    }
    case LINUX_SYS_fstat64: {
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        FsFileInfo info;
        memset(&info, 0, sizeof(info));
        if (file->terminal) { info.mode = 0020666; info.links = 1; }
        else if (file->pipe) { info.mode = 0010600; info.links = 1; info.size = file->pipe->used; }
        else {
            int error = fs_file_info(file->name, &info);
            if (error) return error == -EACCES ? error : -EIO;
        }
        return copy_stat64(b, &info);
    }
    case LINUX_SYS_getdents: {
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (!file->directory) return -20;
        if (!linux_user_range(b, c)) return -EFAULT;
        return fs_getdents(file->name, (void *)(uintptr_t)b, c, &file->position);
    }
    case 220: { /* getdents64: the getdents records, widened (opendir in modern glibc) */
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (!file->directory) return -20;
        if (!linux_user_range(b, c)) return -EFAULT;
        static unsigned char old[1024];
        unsigned int out = 0;
        for (;;) {
            unsigned int position = file->position;
            int n = fs_getdents(file->name, old, sizeof(old), &position);
            if (n < 0) return out ? (int)out : n;
            if (!n) return (int)out;
            for (int at = 0; at < n;) {
                unsigned char *r = old + at;
                unsigned int ino = r[0] | r[1] << 8 | r[2] << 16 | (unsigned int)r[3] << 24;
                unsigned int next = r[4] | r[5] << 8 | r[6] << 16 | (unsigned int)r[7] << 24;
                unsigned int length = r[8] | r[9] << 8, name = (unsigned int)strlen((const char *)r + 10);
                unsigned int need = (19 + name + 1 + 7) & ~7U;
                if (out + need > c) return out ? (int)out : -LINUX_EINVAL;
                unsigned char *d = (unsigned char *)(uintptr_t)b + out;
                memset(d, 0, (int)need);
                memcpy(d, &ino, 4); memcpy(d + 8, &next, 4);
                d[16] = (unsigned char)need; d[17] = (unsigned char)(need >> 8); d[18] = length ? r[length - 1] : 0;
                memcpy(d + 19, r + 10, (int)name);
                out += need; file->position = next;
                if (!length) break;
                at += (int)length;
            }
        }
    }
    case 311: return 0;              /* set_robust_list: no threads to clean up after */
    case 383: case 386: return -LINUX_ENOSYS;   /* statx, rseq: glibc falls back quietly */
    case 355: /* getrandom */
        if (!linux_user_range(a, b)) return -EFAULT;
        net_random((void *)(uintptr_t)a, b);
        return (int)b;
    case 39:
    case 296: { /* mkdir, mkdirat */
        if (frame->eax == 296) { if (a != (unsigned int)-100) return -LINUX_EBADF; a = b; }
        char path[PATH_SIZE]; int error = copy_path(path, a);
        if (error) return error;
        if (fs_is_dir(path) || fs_file_size(path) >= 0) return -17;   /* EEXIST */
        char parent[PATH_SIZE]; strcpy(parent, path);
        char *slash = 0;
        for (char *q = parent; *q; q++) if (*q == '/') slash = q;
        if (slash) { if (slash == parent) slash[1] = 0; else *slash = 0; if (!fs_is_dir(parent)) return -ENOENT; }
        return fs_mkdir(path) ? -13 : 0;
    }
    case 40: { /* rmdir */
        char path[PATH_SIZE]; int error = copy_path(path, a);
        return error ? error : fs_rmdir(path) ? -39 : 0;   /* ENOTEMPTY (or missing) */
    }
    case 85:
    case 305: { /* readlink, readlinkat: Barnix has no symbolic links */
        char path[PATH_SIZE]; int error = copy_path(path, frame->eax == 305 ? b : a);
        if (error) return error;
        return fs_is_dir(path) || fs_file_size(path) >= 0 ? -LINUX_EINVAL : -ENOENT;
    }
    case 76:
    case 191: /* getrlimit, ugetrlimit: no limits */
        if (!linux_user_range(b, 8)) return -EFAULT;
        memset((void *)(uintptr_t)b, 0xff, 8);
        return 0;
    case 340: /* prlimit64 (pid, resource, new, old) */
        if (frame->esi) { if (!linux_user_range(frame->esi, 16)) return -EFAULT; memset((void *)(uintptr_t)frame->esi, 0xff, 16); }
        return 0;
    case 242: /* sched_getaffinity: one processor */
        if (b < 4 || !linux_user_range(c, b < 128 ? b : 128)) return -LINUX_EINVAL;
        memset((void *)(uintptr_t)c, 0, (int)(b < 128 ? b : 128)); *(unsigned char *)(uintptr_t)c = 1;
        return 4;
    case 186: /* sigaltstack: none */
        if (b) { if (!linux_user_range(b, 12)) return -EFAULT; memset((void *)(uintptr_t)b, 0, 12); ((unsigned int *)(uintptr_t)b)[1] = 2; }
        return 0;
    case 77: /* getrusage */
        if (!linux_user_range(b, 72)) return -EFAULT;
        memset((void *)(uintptr_t)b, 0, 72);
        return 0;
    case 266:
    case 406: /* clock_getres, clock_getres_time64: a microsecond */
        if (b) {
            unsigned int words = frame->eax == 406 ? 4 : 2;
            if (!linux_user_range(b, words * 4)) return -EFAULT;
            memset((void *)(uintptr_t)b, 0, (int)words * 4);
            ((unsigned int *)(uintptr_t)b)[words == 4 ? 2 : 1] = 1000;
        }
        return 0;
    case LINUX_SYS_getcwd:
        if (!b) return -LINUX_EINVAL;
        if (!linux_user_range(a, b)) return -EFAULT;
        return fs_getcwd((char *)(uintptr_t)a, b);
    case LINUX_SYS_chdir: {
        char path[PATH_SIZE]; int error = copy_path(path, a);
        return error ? error : fs_cd(path) ? -ENOENT : 0;
    }
    case LINUX_SYS_access: {
        char path[PATH_SIZE]; int error = copy_path(path, a);
        if (error) return error;
        if (b & ~7U) return -LINUX_EINVAL;
        if(((b&4)&&!check_permission(path,'r'))||((b&2)&&!check_permission(path,'w'))||((b&1)&&!check_permission(path,'x')))return -13;
        return fs_file_size(path) >= 0 || fs_is_dir(path) ? 0 : -ENOENT;
    }
    case LINUX_SYS_ioctl: {
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (file->pipe && b == 0x541b) { /* FIONREAD */
            if (!linux_user_range(c, 4)) return -EFAULT;
            memcpy((void *)(uintptr_t)c, &file->pipe->used, 4);
            return 0;
        }
        if (file->terminal) {   /* the console: isatty() and a line-editing tty */
            if (b == 0x5401 || b == 0x802c542aU) {   /* TCGETS, TCGETS2 (newer glibc): canonical mode with echo */
                unsigned int size = b == 0x5401 ? 36 : 44;
                if (!linux_user_range(c, size)) return -EFAULT;
                unsigned int *t = (unsigned int *)(uintptr_t)c;
                memset(t, 0, (int)size);
                if (size == 44) { t[9] = 38400; t[10] = 38400; }   /* c_ispeed, c_ospeed */
                t[0] = 0x100;          /* ICRNL */
                t[1] = 0x5;            /* OPOST | ONLCR */
                t[2] = 0xbf;           /* B38400 | CS8 | CREAD */
                t[3] = 0x8a3b;         /* ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN */
                unsigned char *cc = (unsigned char *)(uintptr_t)c + 17;
                cc[0] = 3; cc[1] = 28; cc[2] = 127; cc[3] = 21; cc[4] = 4; cc[6] = 1;   /* ^C ^\ DEL ^U ^D, VMIN 1 */
                return 0;
            }
            if ((b >= 0x5402 && b <= 0x5404) || (b >= 0x402c542bU && b <= 0x402c542dU)) return 0;   /* TCSETS*, TCSETS2*: the console keeps its mode */
            if (b == 0x5413) {   /* TIOCGWINSZ */
                if (!linux_user_range(c, 8)) return -EFAULT;
                unsigned short *size = (unsigned short *)(uintptr_t)c;
                size[0] = 25; size[1] = 80; size[2] = 640; size[3] = 400;
                return 0;
            }
            if (b == 0x540f) {   /* TIOCGPGRP */
                if (!linux_user_range(c, 4)) return -EFAULT;
                *(int *)(uintptr_t)c = 1; return 0;
            }
            {   /* others are noted on the debug console, as missing system calls */
                char note[] = "[linux: tty ioctl 00000000 is missing]\n";
                for (int i = 0; i < 8; i++) note[17 + i] = "0123456789abcdef"[(b >> (28 - 4 * i)) & 15];
                for (const char *q = note; *q; q++) __asm__ volatile("outb %0, %1" : : "a"(*q), "Nd"((unsigned short)0xe9));
            }
        }
        return -ENOTTY;
    }
    case LINUX_SYS_uname: {
        if (!linux_user_range(a, 6 * 65)) return -EFAULT;
        char *out = (char *)(uintptr_t)a;
        const char *values[6] = {"Barnix", "barnix", "0.1", "Linux ABI experimental", "i686", ""};
        memset(out, 0, 6 * 65);
        for (int i = 0; i < 6; i++) strcpy(out + i * 65, values[i]);
        return 0;
    }
    case LINUX_SYS_readv:
    case LINUX_SYS_writev: {
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (file->pipe) return pipe_transfer(a, b, c, frame->eax == LINUX_SYS_writev, 1);
        if (c > 1024) return -LINUX_EINVAL;
        if (!c) return 0;
        if (!linux_user_range(b, c * 8)) return -EFAULT;
        const unsigned int *iov = (const unsigned int *)(uintptr_t)b;
        unsigned int sum = 0;
        for (unsigned int i = 0; i < c; i++) {
            if (iov[i * 2 + 1] > 0x7fffffffU - sum) return -LINUX_EINVAL;
            sum += iov[i * 2 + 1];
        }
        unsigned int total = 0;
        for (unsigned int i = 0; i < c; i++) {
            int result = transfer(a, iov[i * 2], iov[i * 2 + 1], frame->eax == LINUX_SYS_writev);
            if (result < 0) return total ? (int)total : result;
            total += result;
            if ((unsigned int)result < iov[i * 2 + 1]) break;
        }
        return total;
    }
    default: {   /* unknown: noted on the debug console (QEMU -debugcon), for porting programs */
        unsigned int n = frame->eax;
        char note[] = "[linux: syscall 000 is missing]\n";
        note[16] = (char)('0' + n / 100 % 10); note[17] = (char)('0' + n / 10 % 10); note[18] = (char)('0' + n % 10);
        for (const char *q = note; *q; q++) __asm__ volatile("outb %0, %1" : : "a"(*q), "Nd"((unsigned short)0xe9));
        return -LINUX_ENOSYS;
    }
    }
}
