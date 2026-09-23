#ifndef BARNIX_LINUX_EXEC_H
#define BARNIX_LINUX_EXEC_H
#include "linux_abi.h"
int linux_run_image(const void *image, unsigned int size, int argc,
                    const char *const *argv, int *status);
int linux_run(const char *path, int argc, const char *const *argv, int *status);
int linux_user_range(unsigned int address, unsigned int size);
void linux_syscall_reset(void);
void linux_syscall_exec(void);
int linux_execve(unsigned int path, unsigned int argv, unsigned int envp);
void linux_exec_apply(LinuxTrapFrame *frame);
void linux_abi_selftest(void);
#endif
