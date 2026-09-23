#ifndef BARNIX_LINUX_ABI_H
#define BARNIX_LINUX_ABI_H

#include "include/uapi/unistd.h"
#define LINUX_SYS_exit __NR_exit
#define LINUX_SYS_fork __NR_fork
#define LINUX_SYS_read __NR_read
#define LINUX_SYS_write __NR_write
#define LINUX_SYS_open __NR_open
#define LINUX_SYS_close __NR_close
#define LINUX_SYS_waitpid __NR_waitpid
#define LINUX_SYS_execve __NR_execve
#define LINUX_SYS_chdir __NR_chdir
#define LINUX_SYS_dup __NR_dup
#define LINUX_SYS_pipe __NR_pipe
#define LINUX_SYS_dup2 __NR_dup2
#define LINUX_SYS_getuid __NR_getuid
#define LINUX_SYS_geteuid __NR_geteuid
#define LINUX_SYS_uname __NR_uname
#define LINUX_SYS_readlink __NR_readlink
#define LINUX_SYS_getppid __NR_getppid
#define LINUX_SYS_set_tid_address __NR_set_tid_address
#define LINUX_SYS_ioctl __NR_ioctl
#define LINUX_SYS_fcntl __NR_fcntl
#define LINUX_SYS_setpgid __NR_setpgid
#define LINUX_SYS_getpgrp __NR_getpgrp
#define LINUX_SYS_rt_sigaction __NR_rt_sigaction
#define LINUX_SYS_rt_sigprocmask __NR_rt_sigprocmask
#define LINUX_SYS_nanosleep __NR_nanosleep
#define LINUX_SYS_gettimeofday __NR_gettimeofday
#define LINUX_SYS_clock_gettime __NR_clock_gettime
#define LINUX_SYS_times __NR_times
#define LINUX_SYS_getdents __NR_getdents
#define LINUX_SYS_select __NR_select
#define LINUX_SYS_readv __NR_readv
#define LINUX_SYS_writev __NR_writev
#define LINUX_SYS_poll __NR_poll
#define LINUX_SYS_lseek __NR_lseek
#define LINUX_SYS_getpid __NR_getpid
#define LINUX_SYS_access __NR_access
#define LINUX_SYS_brk __NR_brk
#define LINUX_SYS_mmap __NR_mmap
#define LINUX_SYS_munmap __NR_munmap
#define LINUX_SYS_mmap2 __NR_mmap2
#define LINUX_SYS_stat __NR_stat
#define LINUX_SYS_fstat __NR_fstat
#define LINUX_SYS_stat64 __NR_stat64
#define LINUX_SYS_getcwd __NR_getcwd
#define LINUX_SYS_exit_group __NR_exit_group
#define LINUX_SYS_getgid __NR_getgid
#define LINUX_SYS_getegid __NR_getegid
#define LINUX_SYS_getuid32 __NR_getuid32
#define LINUX_SYS_getgid32 __NR_getgid32
#define LINUX_SYS_geteuid32 __NR_geteuid32
#define LINUX_SYS_getegid32 __NR_getegid32

enum { LINUX_ENOSYS = 38, LINUX_EBADF = 9, LINUX_EINVAL = 22 };

typedef struct {
    unsigned int eax, ebx, ecx, edx, esi, edi, ebp;
} LinuxSyscallFrame;

/* Saved by linux_int80.S: pusha, segment registers, CPU privilege frame. */
typedef struct {
    unsigned int edi, esi, ebp, saved_esp, ebx, edx, ecx, eax;
    unsigned int gs, fs, es, ds;
    unsigned int eip, cs, eflags, user_esp, ss;
} LinuxTrapFrame;
_Static_assert(sizeof(LinuxTrapFrame) == 68, "i386 syscall trap frame");

int linux_syscall_dispatch(LinuxSyscallFrame *frame);

/* Internal outcomes, outside the Linux errno range; never returned to users. */
enum { LINUX_IO_WAIT = -4096, LINUX_IO_SIGPIPE = -4097 };
int linux_io_ready(unsigned int slot);
int linux_io_resume(void);

#endif
