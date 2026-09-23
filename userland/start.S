.section .text
.global _start
.type _start,@function
_start:
    xorl %ebp, %ebp
    movl (%esp), %eax
    leal 4(%esp), %edx
    leal 8(%esp,%eax,4), %ecx
    andl $-16, %esp
    subl $4, %esp
    pushl %ecx
    pushl %edx
    pushl %eax
    call main
    movl %eax, %ebx
    movl $1, %eax
    int $0x80
    ud2
.size _start, . - _start
.section .note.GNU-stack,"",@progbits
