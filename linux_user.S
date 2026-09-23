.section .bss
.align 4
linux_saved_stack: .long 0

.section .text
.global linux_enter_user
.type linux_enter_user,@function
linux_enter_user:
    pushfl
    cli
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl %esp, linux_saved_stack
    movl 24(%esp), %edx
    movl 28(%esp), %ecx
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pushl $0x23
    pushl %ecx
    /* IRQ routing is not installed yet: IF=0, IOPL=0. */
    pushl $0x2
    pushl $0x1b
    pushl %edx
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    iret
.size linux_enter_user, . - linux_enter_user

.global linux_leave_user
.type linux_leave_user,@function
linux_leave_user:
    movl 4(%esp), %edx
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movl linux_saved_stack, %esp
    movl %edx, %eax
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    popfl
    ret
.size linux_leave_user, . - linux_leave_user
.section .note.GNU-stack,"",@progbits
