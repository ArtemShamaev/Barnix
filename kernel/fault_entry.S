/* CPU exception entry for the kernel and native programs (both ring 0).
 * Without these the CPU escalates any fault to a triple fault and the
 * machine silently reboots. Every stub pushes a zero error code where the
 * CPU does not, then the vector, and joins fault_common. */
.section .text
.macro FAULT n
fault_stub_\n:
    pushl $0
    pushl $\n
    jmp fault_common
.endm
.macro FAULT_ERROR n
fault_stub_\n:
    pushl $\n
    jmp fault_common
.endm
FAULT 0
FAULT 1
FAULT 2
FAULT 3
FAULT 4
FAULT 5
FAULT 6
FAULT 7
FAULT_ERROR 8
FAULT 9
FAULT_ERROR 10
FAULT_ERROR 11
FAULT_ERROR 12
FAULT_ERROR 13
FAULT_ERROR 14
FAULT 15
FAULT 16
FAULT_ERROR 17
FAULT 18
FAULT 19
FAULT 20
FAULT_ERROR 21
FAULT 22
FAULT 23
FAULT 24
FAULT 25
FAULT 26
FAULT 27
FAULT 28
FAULT_ERROR 29
FAULT_ERROR 30
FAULT 31

/* Stack: edi..eax (pusha), vector, error, eip, cs, eflags. The handler
 * never returns: it reports and halts. */
fault_common:
    cld
    pusha
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %esp, %eax
    and $-16, %esp
    sub $12, %esp
    push %eax
    call fault_report
1:  cli
    hlt
    jmp 1b

/* Double fault: entered through a task gate on its own TSS and stack, so a
 * kernel or program stack overflow can still be reported. */
.global fault_double_entry
fault_double_entry:
    cli
    cld
    mov $fault_double_stack_top, %esp
    call fault_double_report
2:  cli
    hlt
    jmp 2b

.section .rodata
.global fault_stubs
fault_stubs:
    .long fault_stub_0, fault_stub_1, fault_stub_2, fault_stub_3
    .long fault_stub_4, fault_stub_5, fault_stub_6, fault_stub_7
    .long fault_stub_8, fault_stub_9, fault_stub_10, fault_stub_11
    .long fault_stub_12, fault_stub_13, fault_stub_14, fault_stub_15
    .long fault_stub_16, fault_stub_17, fault_stub_18, fault_stub_19
    .long fault_stub_20, fault_stub_21, fault_stub_22, fault_stub_23
    .long fault_stub_24, fault_stub_25, fault_stub_26, fault_stub_27
    .long fault_stub_28, fault_stub_29, fault_stub_30, fault_stub_31

.section .bss
.align 16
fault_double_stack: .skip 8192
.global fault_double_stack_top
fault_double_stack_top:

.section .note.GNU-stack,"",@progbits
