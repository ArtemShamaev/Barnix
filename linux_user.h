#ifndef BARNIX_LINUX_USER_H
#define BARNIX_LINUX_USER_H
/* Single synchronous task; exit restores the calling kernel context. */
int linux_enter_user(unsigned int entry, unsigned int user_stack);
__attribute__((noreturn)) void linux_leave_user(int status);
#endif
