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
    movl %esp, %eax          /* error code, eip, cs, eflags */
    andl $-16, %esp
    subl $12, %esp
    pushl %eax
    call linux_kernel_fault
    ud2

/* One entry per CPU exception: it notes the vector (for the kernel-fault
 * report) and joins the common path. */
.section .bss
.global linux_fault_vector
linux_fault_vector: .long 0
.section .text
.macro FAULT_STUB n, target
linux_fault_stub_\n:
    movl $\n, linux_fault_vector
    jmp \target
.endm
FAULT_STUB 0, linux_user_fault
FAULT_STUB 1, linux_user_fault
FAULT_STUB 2, linux_user_fault
FAULT_STUB 3, linux_user_fault
FAULT_STUB 4, linux_user_fault
FAULT_STUB 5, linux_user_fault
FAULT_STUB 6, linux_user_fault
FAULT_STUB 7, linux_user_fault
FAULT_STUB 8, linux_user_fault_error
FAULT_STUB 9, linux_user_fault
FAULT_STUB 10, linux_user_fault_error
FAULT_STUB 11, linux_user_fault_error
FAULT_STUB 12, linux_user_fault_error
FAULT_STUB 13, linux_user_fault_error
FAULT_STUB 14, linux_user_fault_error
FAULT_STUB 15, linux_user_fault
FAULT_STUB 16, linux_user_fault
FAULT_STUB 17, linux_user_fault_error
FAULT_STUB 18, linux_user_fault
FAULT_STUB 19, linux_user_fault
FAULT_STUB 20, linux_user_fault
FAULT_STUB 21, linux_user_fault_error
FAULT_STUB 22, linux_user_fault
FAULT_STUB 23, linux_user_fault
FAULT_STUB 24, linux_user_fault
FAULT_STUB 25, linux_user_fault
FAULT_STUB 26, linux_user_fault
FAULT_STUB 27, linux_user_fault
FAULT_STUB 28, linux_user_fault
FAULT_STUB 29, linux_user_fault_error
FAULT_STUB 30, linux_user_fault_error
FAULT_STUB 31, linux_user_fault
.section .rodata
.global linux_fault_stubs
linux_fault_stubs:
    .long linux_fault_stub_0
    .long linux_fault_stub_1
    .long linux_fault_stub_2
    .long linux_fault_stub_3
    .long linux_fault_stub_4
    .long linux_fault_stub_5
    .long linux_fault_stub_6
    .long linux_fault_stub_7
    .long linux_fault_stub_8
    .long linux_fault_stub_9
    .long linux_fault_stub_10
    .long linux_fault_stub_11
    .long linux_fault_stub_12
    .long linux_fault_stub_13
    .long linux_fault_stub_14
    .long linux_fault_stub_15
    .long linux_fault_stub_16
    .long linux_fault_stub_17
    .long linux_fault_stub_18
    .long linux_fault_stub_19
    .long linux_fault_stub_20
    .long linux_fault_stub_21
    .long linux_fault_stub_22
    .long linux_fault_stub_23
    .long linux_fault_stub_24
    .long linux_fault_stub_25
    .long linux_fault_stub_26
    .long linux_fault_stub_27
    .long linux_fault_stub_28
    .long linux_fault_stub_29
    .long linux_fault_stub_30
    .long linux_fault_stub_31
.section .note.GNU-stack,"",@progbits
