.section .rodata
.global linux_probe_start, linux_probe_end
linux_probe_start:
.incbin "userland/linux-probe.elf"
linux_probe_end:
.section .note.GNU-stack,"",@progbits
