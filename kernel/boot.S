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
#ifdef BARNIX_EFI
    /* EFI GRUB may leave PAE enabled with paging off. Barnix's process page
     * tables are non-PAE, so normalize CPU state before enabling them. */
    xor %eax, %eax
    mov %eax, %cr4
#endif
    lgdt gdt_descriptor
    ljmp $GDT_CODE_SEGMENT, $gdt_loaded

gdt_loaded:
    mov $GDT_DATA_SEGMENT, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* Statically linked glibc and modern GCC use XMM registers at startup. */
    mov %cr0, %eax
    and $0xfffffff3, %eax
    or $0x2, %eax
    mov %eax, %cr0
    mov %cr4, %eax
    or $0x600, %eax
    mov %eax, %cr4
    fninit

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
    /* Linux i386 TLS uses GDT entry 6 via set_thread_area. */
    .global gdt_tls_descriptor
gdt_tls_descriptor:
    .quad 0
    /* Entry 7: TSS of the double-fault task (kernel/fault.c). */
    .global gdt_fault_tss_descriptor
gdt_fault_tss_descriptor:
    .quad 0
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.section .bss
.align 16
kernel_stack_bottom:
.skip 131072
.global kernel_stack_top
kernel_stack_top:

.section .note.GNU-stack,"",@progbits
