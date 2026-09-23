#include "linux_abi.h"
#include "linux_exec.h"
#include "linux_task.h"
#include "linux_user.h"
#include "linux_memory.h"
#include "fs.h"
#include "barnix.h"
#include "keyboard.h"

/* A deliberately bounded, synchronous Linux i386 ABI. Unsupported operations
 * fail with ENOSYS; they must not report success without doing the work. */
enum { FD_COUNT = 16, PATH_SIZE = 128, EFAULT = 14, ENOENT = 2,
       EIO = 5, EACCES = 13, EMFILE = 24, ENOTTY = 25, ESPIPE = 29,
       EISDIR = 21, ENAMETOOLONG = 36, EFBIG = 27, EAGAIN = 11, ENFILE = 23 };
enum { O_ACCMODE = 3, O_WRONLY = 1, O_RDWR = 2, O_CREAT = 64,
       O_EXCL = 128, O_TRUNC = 512, O_APPEND = 1024, O_NONBLOCK = 2048,
       O_DIRECTORY = 65536, O_CLOEXEC = 02000000 };
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
#define files task_files[file_task]
#define descriptor_flags task_flags[file_task]
static unsigned char write_buffer[FS_MAX_FILE_SIZE];

void linux_syscall_reset(void)
{
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
}
void linux_files_switch(unsigned int slot) { file_task = slot; }
void linux_files_fork(unsigned int parent, unsigned int child)
{
    pending_io[child].active = 0;
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
    memcpy((void *)address, fds, sizeof(fds));
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
        void *user = (void *)(vector->address + io->offset);
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
        if (count) memcpy(io->vectors, (const void *)address, count * sizeof(IoVector));
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
        out[i] = *(const char *)address++;
        if (!out[i]) return i ? 0 : -ENOENT;
    }
    return -ENAMETOOLONG;
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
    unsigned char *data = (unsigned char *)address;
    if (file->terminal) {
        if (!writing) { data[0] = (unsigned char)getch(); return 1; }
        for (unsigned int i = 0; i < count; i++) {
            char character[2] = {(char)data[i], 0};
            print(WHITE, character);
        }
        return (int)count;
    }
    if (!writing) {
        int got = fs_read(file->name, file->position, data, count);
        if (got < 0) return -EIO;
        file->position += got;
        return got;
    }
    int size = fs_size(file->name);
    if (size < 0) return -EIO;
    unsigned int position = file->flags & O_APPEND ? (unsigned int)size : file->position;
    if (position > sizeof(write_buffer) || count > sizeof(write_buffer) - position) return -EFBIG;
    unsigned int end = position + count;
    unsigned int new_size = end > (unsigned int)size ? end : (unsigned int)size;
    memset(write_buffer, 0, new_size);
    if (fs_read(file->name, 0, write_buffer, size) != size) return -EIO;
    memcpy(write_buffer + position, data, count);
    if (fs_write(file->name, (const char *)write_buffer, new_size)) return -EIO;
    file->position = end;
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
    case LINUX_SYS_getpid:
    case LINUX_SYS_getpgrp: return 1;
    case LINUX_SYS_getppid: return 0;
    case LINUX_SYS_getuid:
    case LINUX_SYS_geteuid:
    case LINUX_SYS_getgid: /* getgid */
    case LINUX_SYS_getegid: /* getegid */
    case LINUX_SYS_getuid32: case LINUX_SYS_getgid32:
    case LINUX_SYS_geteuid32: case LINUX_SYS_getegid32: /* 32-bit IDs */
        return 0;
    case LINUX_SYS_read: return transfer(a, b, c, 0);
    case LINUX_SYS_write: return transfer(a, b, c, 1);
    case __NR_pipe: return create_pipe(a, 0);
    case __NR_pipe2: return create_pipe(a, b);
    case LINUX_SYS_close: return close_fd(a);
    case LINUX_SYS_open: {
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
        if (b & ~(O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC))
            return -LINUX_EINVAL;
        int fd = free_fd(0);
        if (fd < 0) return fd;
        OpenFile *file = 0;
        for (int i = 0; i < FD_COUNT * LINUX_TASK_LIMIT; i++) if (!descriptions[i].refs) { file = &descriptions[i]; break; }
        if (!file) return -EMFILE;
        int directory = fs_is_dir(path);
        if (directory && ((b & O_ACCMODE) || (b & (O_CREAT | O_TRUNC)))) return -EISDIR;
        int size = directory ? 0 : fs_size(path);
        if ((b & O_DIRECTORY) && !directory) return size < 0 ? -ENOENT : -20;
        if (size >= 0 && (b & O_CREAT) && (b & O_EXCL)) return -17;
        if (size < 0) {
            if (!(b & O_CREAT)) return -ENOENT;
            if (fs_write(path, "", 0)) return -EIO;
        }
        if ((b & O_TRUNC) && (b & O_ACCMODE) && fs_write(path, "", 0)) return -EIO;
        memset(file, 0, sizeof(*file));
        file->refs = 1; file->flags = b & (O_ACCMODE | O_APPEND | O_NONBLOCK | O_DIRECTORY);
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
    case LINUX_SYS_fcntl: {
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
            if (c & ~(O_ACCMODE | O_APPEND | O_NONBLOCK | O_DIRECTORY)) return -LINUX_EINVAL;
            if (file->terminal && (c & O_NONBLOCK)) return -LINUX_EINVAL;
            file->flags = (file->flags & (O_ACCMODE | O_DIRECTORY)) | (c & (O_APPEND | O_NONBLOCK));
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
        int size = fs_size(file->name);
        if (size < 0) return -EIO;
        unsigned int base = c == 2 ? (unsigned int)size : c == 1 ? file->position : 0;
        long long position = (long long)base + (int)b;
        if (position < 0) return -LINUX_EINVAL;
        if (position > 0x7fffffff) return -75; /* EOVERFLOW */
        file->position = (unsigned int)position;
        return (int)position;
    }
    case LINUX_SYS_getdents: {
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (!file->directory) return -20;
        if (!linux_user_range(b, c)) return -EFAULT;
        return fs_getdents(file->name, (void *)b, c, &file->position);
    }
    case LINUX_SYS_getcwd:
        if (!b) return -LINUX_EINVAL;
        if (!linux_user_range(a, b)) return -EFAULT;
        return fs_getcwd((char *)a, b);
    case LINUX_SYS_chdir: {
        char path[PATH_SIZE]; int error = copy_path(path, a);
        return error ? error : fs_cd(path) ? -ENOENT : 0;
    }
    case LINUX_SYS_access: {
        char path[PATH_SIZE]; int error = copy_path(path, a);
        if (error) return error;
        if (b & ~7U) return -LINUX_EINVAL;
        /* Permission metadata is not exposed by the current VFS. */
        if (b) return -LINUX_ENOSYS;
        return fs_size(path) >= 0 || fs_is_dir(path) ? 0 : -ENOENT;
    }
    case LINUX_SYS_ioctl: {
        OpenFile *file = get_file(a);
        if (!file) return -LINUX_EBADF;
        if (file->pipe && b == 0x541b) { /* FIONREAD */
            if (!linux_user_range(c, 4)) return -EFAULT;
            memcpy((void *)c, &file->pipe->used, 4);
            return 0;
        }
        return -ENOTTY;
    }
    case LINUX_SYS_uname: {
        if (!linux_user_range(a, 6 * 65)) return -EFAULT;
        char *out = (char *)a;
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
        const unsigned int *iov = (const unsigned int *)b;
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
    default: return -LINUX_ENOSYS;
    }
}
