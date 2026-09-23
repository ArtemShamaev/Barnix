#ifndef BARNIX_LINUX_TASK_H
#define BARNIX_LINUX_TASK_H
#include "linux_abi.h"
#define LINUX_TASK_LIMIT 3
void linux_task_init(void);
int linux_task_syscall(LinuxTrapFrame *frame);
void linux_task_fault_from_regs(LinuxTrapFrame *frame);
void linux_task_io_result(LinuxTrapFrame *frame, int result);
void linux_files_fork(unsigned int parent, unsigned int child);
void linux_files_switch(unsigned int slot);
void linux_files_exit(unsigned int slot);
#endif
