/* The class library, linked into the VM. */
    .section .rodata
    .globl bjvm_rt
    .globl bjvm_rt_size
    .balign 16
bjvm_rt:
    .incbin RT_JAR
bjvm_rt_end:
    .balign 4
bjvm_rt_size:
    .long bjvm_rt_end - bjvm_rt
    .section .note.GNU-stack,"",@progbits
