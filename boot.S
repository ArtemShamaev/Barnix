#define MULTIBOOT_MAGIC 0x1BADB002
#define MULTIBOOT_FLAGS 0x00000000
#define MULTIBOOT_CHECKSUM -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

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
    mov $kernel_stack_top, %esp
    push %ebx
    push %eax
    call kernel_main

.hang:
    hlt
    jmp .hang

.section .bss
.align 16
kernel_stack_bottom:
.skip 16384
kernel_stack_top:

.section .note.GNU-stack,"",@progbits
