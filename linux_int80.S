.section .text
.global linux_int80
.type linux_int80,@function
linux_int80:
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    cld
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movl %esp, %ebx
    andl $-16, %esp
    subl $12, %esp
    pushl %ebx
    call linux_syscall_from_regs
    movl %ebx, %esp
linux_restore_frame:
    popa
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret
.size linux_int80, . - linux_int80

/* User exceptions terminate the current process. Normalize the varying CPU
 * exception frames before scheduling another process (some have an error code). */
.global linux_user_fault
.type linux_user_fault,@function
linux_user_fault:
    pushl $0
.global linux_user_fault_error
linux_user_fault_error:
    cld
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    testb $3, 8(%esp)
    jz kernel_fault
    addl $4, %esp            /* discard normalized exception error code */
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    movl %esp, %ebx
    andl $-16, %esp
    subl $12, %esp
    pushl %ebx
    call linux_task_fault_from_regs
    movl %ebx, %esp
    jmp linux_restore_frame
.size linux_user_fault, . - linux_user_fault
kernel_fault:
    andl $-16, %esp
    subl $12, %esp
    pushl $fault_message
    call panic
    ud2
.section .rodata
fault_message: .asciz "Fault in Linux ABI kernel handler"
.section .note.GNU-stack,"",@progbits
