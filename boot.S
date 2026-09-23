#define GDT_CODE_SEGMENT 0x08
#define GDT_DATA_SEGMENT 0x10

.section .text.boot
.global _start
.extern kernel_main
.type _start, @function
_start:
    cli
    cld
    mov %eax, %edi
    mov %ebx, %esi
    lgdt gdt_descriptor
    ljmp $GDT_CODE_SEGMENT, $gdt_loaded

gdt_loaded:
    mov $GDT_DATA_SEGMENT, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    mov $kernel_stack_top, %esp
    xor %ebp, %ebp
    sub $8, %esp
    push %esi
    push %edi
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

.section .rodata
.align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    /* Ring-3 flat code/data segments for Linux compatibility tasks. */
    .quad 0x00CFFA000000FFFF
    .quad 0x00CFF2000000FFFF
    .global gdt_tss_descriptor
gdt_tss_descriptor:
    .quad 0
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.section .bss
.align 16
kernel_stack_bottom:
.skip 16384
.global kernel_stack_top
kernel_stack_top:

.section .note.GNU-stack,"",@progbits
