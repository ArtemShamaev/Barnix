.section .text
.global elf_enter
.type elf_enter, @function
elf_enter:
    push %ebp
    mov %esp, %ebp
    push %ebx
    push %esi
    push %edi
    pushfl
    mov %esp, saved_kernel_stack
    mov 8(%ebp), %eax
    mov 12(%ebp), %edx
    mov 16(%ebp), %ecx
    mov 20(%ebp), %ebx
    mov 24(%ebp), %esp
    and $-16, %esp
    sub $4, %esp
    push %ebx
    push %ecx
    push %edx
    call *%eax
    cld
    mov saved_kernel_stack, %esp
    popfl
    pop %edi
    pop %esi
    pop %ebx
    leave
    ret
.size elf_enter, . - elf_enter
.section .bss
.align 4
saved_kernel_stack:
    .long 0
.section .note.GNU-stack,"",@progbits
