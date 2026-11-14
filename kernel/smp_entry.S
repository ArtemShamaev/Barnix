/* Real-mode entry for application processors, copied to a page below 1 MiB.
 * The boot processor patches the fields at the end before the start-up IPI. */
    .section .rodata
    .code16
    .global smp_trampoline, smp_trampoline_end
smp_trampoline:
    cli
    cld
    mov %cs, %ax
    mov %ax, %ds
    lgdtl tr_gdtr - smp_trampoline
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmpl *(tr_far - smp_trampoline)
    .code32
tr_protected:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    .byte 0xbb              /* mov $base, %ebx */
    .global smp_tr_base
smp_tr_base = . - smp_trampoline
    .long 0
    mov $1, %eax
    lock xadd %eax, tr_arrived - smp_trampoline(%ebx)
    cmp tr_max - smp_trampoline(%ebx), %eax
    jae 2f
    lea 1(%eax), %edx
    imul tr_stack_bytes - smp_trampoline(%ebx), %edx
    add tr_stacks - smp_trampoline(%ebx), %edx
    mov %edx, %esp
    lgdt tr_kernel_gdtr - smp_trampoline(%ebx)
    push %eax
    call *tr_entry - smp_trampoline(%ebx)
2:  cli
    hlt
    jmp 2b

    .align 8
tr_gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
tr_gdtr:
    .word 23
    .global smp_tr_gdt
smp_tr_gdt = . - smp_trampoline
    .long 0
    .global smp_tr_far
smp_tr_far = . - smp_trampoline
tr_far:
    .long 0
    .word 0x08
    .global smp_tr_protected
smp_tr_protected = tr_protected - smp_trampoline
    .global smp_tr_gdt_offset
smp_tr_gdt_offset = tr_gdt - smp_trampoline
    .align 4
    .global smp_tr_fields
smp_tr_fields = . - smp_trampoline
tr_arrived: .long 0
tr_max: .long 0
tr_stack_bytes: .long 0
tr_stacks: .long 0
tr_entry: .long 0
tr_kernel_gdtr: .word 0
    .long 0
smp_trampoline_end:

    .text
    .code32
/* smp_idle(&generation, seen): hlt unless the generation changed. An NMI
 * that lands between the check and hlt resumes after hlt, so no wake-up
 * is lost. */
    .global smp_idle, smp_nmi
smp_idle:
    mov 4(%esp), %edx
    mov 8(%esp), %ecx
idle_window:
    cmp (%edx), %ecx
    jne idle_done
    hlt
idle_done:
    ret
smp_nmi:
    push %eax
    mov 4(%esp), %eax
    cmp $idle_window, %eax
    jb 1f
    cmp $idle_done, %eax
    jae 1f
    movl $idle_done, 4(%esp)
1:  pop %eax
    iret

    .section .note.GNU-stack,"",@progbits
