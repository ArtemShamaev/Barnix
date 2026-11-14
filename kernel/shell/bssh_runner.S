.section .rodata
.global bssh_runner_start, bssh_runner_end
bssh_runner_start:
.incbin "userland/bssh-run.elf"
bssh_runner_end:
.section .note.GNU-stack,"",@progbits
