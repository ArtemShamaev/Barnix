/* x86_64 entry. GRUB (Multiboot v1) starts us in 32-bit protected mode with
 * paging off, on Legacy BIOS and on UEFI alike. We build identity-mapped page
 * tables for the first 4 GiB (2 MiB pages: kernel, framebuffer, LAPIC and
 * 32-bit MMIO), enable long mode and call kernel_main(magic, info). */
#define CR0_PG  0x80000000
#define CR4_PAE 0x20
#define EFER    0xC0000080
#define EFER_LME 0x100

.section .text.boot
.code32
.global _start
.type _start, @function
_start:
    cli
    cld
    mov %eax, %edi              /* Multiboot magic */
    mov %ebx, %esi              /* Multiboot information (physical) */
    mov $boot_stack_top, %esp

    /* Long mode is required: CPUID 0x80000001 EDX bit 29. */
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb no_long_mode
    mov $0x80000001, %eax
    cpuid
    bt $29, %edx
    jnc no_long_mode

    /* PML4[0] -> PDPT; PDPT[0..3] -> four page directories of 2 MiB pages. */
    mov $boot_pdpt, %eax
    or $0x3, %eax
    mov %eax, boot_pml4
    xor %ecx, %ecx
1:  mov %ecx, %eax
    shl $12, %eax
    add $boot_pd, %eax
    or $0x3, %eax
    mov %eax, boot_pdpt(,%ecx,8)
    inc %ecx
    cmp $4, %ecx
    jne 1b
    xor %ecx, %ecx              /* 2048 entries x 2 MiB = 4 GiB */
2:  mov %ecx, %eax
    shl $21, %eax
    or $0x83, %eax              /* present, writable, 2 MiB page */
    mov %eax, boot_pd(,%ecx,8)
    movl $0, boot_pd+4(,%ecx,8)
    inc %ecx
    cmp $2048, %ecx
    jne 2b

    /* EFI GRUB may leave CR4 bits set; start from a known state. */
    mov $CR4_PAE, %eax
    mov %eax, %cr4
    mov $boot_pml4, %eax
    mov %eax, %cr3
    mov $EFER, %ecx
    rdmsr
    or $EFER_LME, %eax
    wrmsr
    mov %cr0, %eax
    or $CR0_PG, %eax
    mov %eax, %cr0

    lgdt gdt64_descriptor
    ljmp $0x08, $long_mode_entry

no_long_mode:
    /* VGA text: the CPU cannot run this kernel. */
    mov $0xb8000, %edi
    mov $no_long_mode_text, %esi
    mov $0x4f, %ah
3:  lodsb
    test %al, %al
    jz 4f
    stosw
    jmp 3b
4:  cli
    hlt
    jmp 4b

.code64
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs
    mov $kernel_stack_top, %rsp
    xor %ebp, %ebp

    /* SSE for user programs (glibc, GCC output); the kernel itself is built
     * without SSE so interrupts never need to save vector state. */
    mov %cr0, %rax
    and $~0xc, %rax             /* clear EM, TS */
    or $0x2, %rax               /* MP */
    mov %rax, %cr0
    mov %cr4, %rax
    or $0x600, %rax             /* OSFXSR, OSXMMEXCPT */
    mov %rax, %cr4
    fninit

    mov %edi, %edi              /* zero-extend magic and info pointer */
    mov %esi, %esi
    call kernel_main
5:  cli
    hlt
    jmp 5b

.section .rodata
no_long_mode_text:
    .asciz "Barnix: this CPU does not support 64-bit long mode."

.align 16
gdt64:
    .quad 0                     /* null */
    .quad 0x00AF9A000000FFFF    /* 0x08: kernel code, 64-bit */
    .quad 0x00CF92000000FFFF    /* 0x10: kernel data */
gdt64_end:
gdt64_descriptor:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.section .bss
.align 4096
boot_pml4: .skip 4096
boot_pdpt: .skip 4096
boot_pd:   .skip 4096 * 4
.align 16
boot_stack: .skip 4096
boot_stack_top:
kernel_stack: .skip 131072
.global kernel_stack_top
kernel_stack_top:

.section .note.GNU-stack,"",@progbits
