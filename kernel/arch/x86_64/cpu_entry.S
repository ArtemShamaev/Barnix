/* x86_64 exception entry. Each stub pushes a zero error code where the CPU
 * does not, then the vector; cpu_common saves the general registers and
 * calls cpu_exception(frame). Faults are reported and the CPU halts. */
.section .text
.code64
.macro STUB n
cpu_stub_\n:
    pushq $0
    pushq $\n
    jmp cpu_common
.endm
.macro STUB_ERROR n
cpu_stub_\n:
    pushq $\n
    jmp cpu_common
.endm
STUB 0
STUB 1
STUB 2
STUB 3
STUB 4
STUB 5
STUB 6
STUB 7
STUB_ERROR 8
STUB 9
STUB_ERROR 10
STUB_ERROR 11
STUB_ERROR 12
STUB_ERROR 13
STUB_ERROR 14
STUB 15
STUB 16
STUB_ERROR 17
STUB 18
STUB 19
STUB 20
STUB_ERROR 21
STUB 22
STUB 23
STUB 24
STUB 25
STUB 26
STUB 27
STUB 28
STUB_ERROR 29
STUB_ERROR 30
STUB 31

/* Frame (low to high): r15..rax (15 registers), vector, error, rip, cs,
 * rflags, rsp, ss. */
cpu_common:
    cld
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rdi
    and $-16, %rsp
    call cpu_exception
1:  cli
    hlt
    jmp 1b

.section .rodata
.align 8
.global cpu_stubs
cpu_stubs:
    .quad cpu_stub_0, cpu_stub_1, cpu_stub_2, cpu_stub_3, cpu_stub_4, cpu_stub_5, cpu_stub_6, cpu_stub_7
    .quad cpu_stub_8, cpu_stub_9, cpu_stub_10, cpu_stub_11, cpu_stub_12, cpu_stub_13, cpu_stub_14, cpu_stub_15
    .quad cpu_stub_16, cpu_stub_17, cpu_stub_18, cpu_stub_19, cpu_stub_20, cpu_stub_21, cpu_stub_22, cpu_stub_23
    .quad cpu_stub_24, cpu_stub_25, cpu_stub_26, cpu_stub_27, cpu_stub_28, cpu_stub_29, cpu_stub_30, cpu_stub_31

.section .note.GNU-stack,"",@progbits
