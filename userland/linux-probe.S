/* No Barnix API: the same ELF runs on an i386-compatible Linux host. */
.section .text
.global _start
_start:
    cmpl $2, (%esp)
    jne fail
    testl $15, %esp
    jnz fail
    movl 8(%esp), %eax
    cmpb $102, (%eax)
    je fault
    cmpb $112, (%eax)
    je privileged
    cmpl $0x75677261, (%eax) /* argu */
    jne fail
    cmpl $0, 12(%esp)       /* argv terminator */
    jne fail
    cmpl $0, 16(%esp)       /* empty envp (test runs with env -i) */
    jne fail
    cmpl $0, zero
    jne fail
    movl $99, zero
    movl $0x12345678, %ebx
    movl $0x23456789, %esi
    movl $0x3456789a, %edi
    movl $0x456789ab, %ebp
    movl $20, %eax
    std
    int $0x80
    pushfl
    popl %edx
    cld
    testl $0x400, %edx
    jz fail
    testl %eax, %eax
    jle fail
    cmpl $0x12345678, %ebx
    jne fail
    cmpl $0x23456789, %esi
    jne fail
    cmpl $0x3456789a, %edi
    jne fail
    cmpl $0x456789ab, %ebp
    jne fail
    movl $0x7fffffff, %eax
    int $0x80
    cmpl $-38, %eax
    jne fail
    movl $4, %eax
    movl $-1, %ebx
    movl $message, %ecx
    movl $message_end-message, %edx
    int $0x80
    cmpl $-9, %eax
    jne fail
    movl $4, %eax
    movl $1, %ebx
    int $0x80
    cmpl $message_end-message, %eax
    jne fail
    movl %esp, %eax
    subl $12, %esp
    pushl %eax
    call file_probe
    addl $16, %esp
    testl %eax, %eax
    jnz file_fail
    movl $252, %eax
    movl $37, %ebx
    int $0x80
file_fail:
    addl $128, %eax
    movl %eax, %ebx
    movl $1, %eax
    int $0x80
fail:
    movl $1, %eax
    movl $99, %ebx
    int $0x80
    ud2
fault:
    /* Must fault: supervisor kernel text, despite being identity-mapped. */
    movl $0, 0x00100000
    jmp fail
privileged:
    cli
    jmp fail
.section .rodata
message: .ascii "Linux ABI probe passed\n"
message_end:
.section .bss
.align 4
zero: .skip 4
.section .note.GNU-stack,"",@progbits
