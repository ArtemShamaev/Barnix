# Linux i386 user programs

`start.S` reads the Linux initial stack, calls `main(argc, argv, envp)`, and exits with
`int 0x80`. `syscall.h` wraps Linux syscall numbers from `include/uapi/unistd.h`.
No legacy Barnix API or host libc is linked.

`commands.c` builds the ten migrated commands selected by `COMMAND`. The build
installs them in `apps/bin/` alongside the remaining legacy programs, and then
into `/bin` on fresh images. The normal i386 GNU linker layout is used.

Build with `make apps`; run `make test-linux-commands` to execute these exact
binaries on Linux. `linux-probe.S` and `linux-file-probe.c` test the entry stack,
registers, descriptors, files and directories on both Linux and Barnix.

To build another freestanding program:

```sh
gcc -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector \
    -nostdlib -static -no-pie -Wl,--build-id=none \
    userland/start.S your-program.c -o your-program.elf
```

Use the wrappers in `userland/syscall.h`. Compiler-emitted library helpers must
be provided by the program. This is not yet a libc/toolchain for building Bash.
See `../README-LINUX-REWRITE.md` for supported syscalls and loader limits.

`make test-exec` checks `exec-parent.elf` and `exec-child.elf` across 34 successive
image replacements, then runs `fork-probe.elf`. The same binaries run on Linux
and in Barnix. Tests cover failed exec, CLOEXEC, shared offsets, argv/env, memory
and cwd separation, wait status, fault recovery, nested forks and slot reuse.
Test executables are not installed in the normal live image.

`make test-pipe` runs `pipe-probe.elf` on Linux and Barnix, including the normal
ISO with a temporary ATA disk. It checks blocking transfers larger than the
pipe buffer, atomic vectored writes from two writers, nonblocking errors and
partial writes, EOF, SIGPIPE, descriptor exhaustion/reuse, and CLOEXEC. Forked
`echo` and `cat` commands communicate through a pipe after exec. The probe links
at a different virtual address from these commands to exercise address-space
switching during pipe I/O. The native test uses a 16-descriptor limit to match
Barnix. Barnix-only launches also check deadlock reporting and recovery.

`bssh-run.elf` is the Linux runner for Bssh pipelines and redirections. It shares
`shell_parse.c` with the kernel shell, runs two children with a pipe between
them, closes unused descriptors, and waits for both. Single commands with
redirections replace the runner directly. The normal kernel embeds this ELF;
it is not an extra `/bin` command and does not require a disk upgrade.
`make test-pipeline` compares the same runner with native `sh` for the supported
syntax and validates keyboard-driven launches in the normal ISO. Plain command
names resolve under `/bin`; explicit paths and the `linux /path` prefix work too.
