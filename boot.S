#define MULTIBOOT_MAGIC 0x1BADB002
#define MULTIBOOT_FLAGS 0x00000000
#define MULTIBOOT_CHECKSUM -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

#define GDT_CODE_SEGMENT 0x08
#define GDT_DATA_SEGMENT 0x10

.section .multiboot
.align 4
.long MULTIBOOT_MAGIC
.long MULTIBOOT_FLAGS
.long MULTIBOOT_CHECKSUM

.section .text.boot
.global _start
.extern kernel_main
.type _start, @function
_start:
    cli
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
    push %ebx
    push %eax
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
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.section .bss
.align 16
kernel_stack_bottom:
.skip 16384
kernel_stack_top:

.section .note.GNU-stack,"",@progbits
