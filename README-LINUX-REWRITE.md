# Barnix Linux ABI

Barnix keeps its own kernel and implements the Linux **i386** userspace ABI.
The normal command launcher now accepts Linux ELF directly:

```text
./linux-probe.elf argument
/bin/echo hello
```

`linux /path/program [args]` remains available to explicitly require Linux ABI.
Bash and ordinary dynamically linked distribution binaries are **not supported
yet**.

## Executable formats

Static, little-endian ELF32 i386 `ET_EXEC` is supported. A Linux image may use
the original 16–20 MiB window or a 4 MiB-aligned virtual window starting at
32 MiB or above and ending below or at `0xc0000000`. Standard linker addresses
such as `0x08048000` work: pages map to the reserved physical arena, not to RAM
at that numerical virtual address. Every load segment must fit in that window,
with the final 64 KiB reserved for the stack. TLS, PIE, ELF interpreters and
images exceeding these limits are rejected before loading.

Linux programs use OSABI 0 (System V) or 3 (Linux), ABI version 0. Rebuilt legacy
Barnix programs carry OSABI 255 (standalone) and ABI version 5, set by
`scripts/mark_barnix_elf.py`. That explicit marker selects the old ring-0
function-pointer ABI. The launcher never retries a failing Linux program as
legacy code. **Old unmarked Barnix binaries must be rebuilt and reinstalled**;
ELF metadata cannot reliably distinguish their entry convention from Linux.

`make run-cd` uses the rebuilt live image. Existing ATA/USB images are preserved;
with QEMU stopped, use `make install-apps` to update commands on the ATA image.
This does not convert arbitrary old custom programs: rebuild those separately.

## Migrated commands

These `/bin` programs now use Linux syscalls, run in ring 3 in Barnix and also
run as the same unmodified binaries on Linux:

`echo`, `pwd`, `cat`, `ls`, `touch`, `write`, `append`, `cp`, `true`, `false`.

Sources live in `userland/commands.c`, with a freestanding startup and syscall
wrappers. They do not link `BarnixAPI` or `barnixiolib`. `cat` preserves raw file
bytes and does not append a newline; `cp` continues to refuse existing targets.
`touch` currently creates a missing file without updating existing timestamps.
Other commands, including device tools, `cd` and the editor, still use the
legacy ABI. These are small commands, not full GNU coreutils implementations.

Bssh now accepts two-command foreground pipelines (`echo hello | cat`) and
`<`, `>`, `>>` for Linux programs. Single/double quotes and backslash escapes
protect literal arguments. A small embedded Linux ELF, built from
`userland/bssh-run.c`, wires descriptors, forks both stages, execs the commands,
and waits for both. It returns the last stage's status, including 128 + signal
for a fault. The parent and two stages occupy all three task slots; commands
that themselves fork may reach that limit. The helper is embedded in the kernel,
so existing disks do not need a new helper installed. Standalone legacy commands
keep the original launcher and can use quoted arguments, but cannot participate
in pipelines or redirections.

## Implemented kernel path

- Ring 3 with a hardware TSS, separate syscall stack, and protected supervisor
  mappings. User exceptions finish with status 139 and restore the caller;
  kernel exceptions stop with a diagnostic.
- `int 0x80` register/DF preservation, negative errno results, `exit/exit_group`.
- Aligned `argc/argv/envp` and auxv including `AT_PHDR`, `AT_PHENT`,
  `AT_PHNUM`, `AT_PAGESZ`, `AT_BASE`, `AT_ENTRY`, user/group IDs, `AT_SECURE` and `AT_EXECFN`.
  Program headers are copied to the stack if they are not mapped by PT_LOAD.
- Regular-file I/O, shared positions through `dup/dup2`, a subset of `fcntl`,
  scatter/gather I/O, `chdir/getcwd`, bounded `brk`, basic IDs and `uname`.
- Anonymous `pipe`/`pipe2` with 4 KiB buffers, blocking `read/write/readv/writev`,
  `O_NONBLOCK`, `O_CLOEXEC`, `F_GETPIPE_SZ` and `FIONREAD`. Writes up to 4096 bytes
  (including all vectors of a `writev`) are atomic. Larger blocking writes
  continue across process switches; nonblocking writes may return a partial
  count. Empty reads wait for data or EOF; writers wait for buffer space.
  Forked and duplicated descriptors keep endpoints alive until their last
  reference closes, including automatic closes on exit and exec. A write with
  no readers terminates the writer with the default SIGPIPE action (status 13
  for `waitpid`, 141 for the launcher). Signal handlers and masks are absent.
  There are at most 24 pipes per launch and 16 descriptors per process.
- Read-only directory descriptors and Linux i386 `getdents` records, including
  correct name offsets, record alignment, directory types and small-buffer errors.
- `execve` replaces the calling image without returning to its old entry point.
  ELF, executable mode bits, user vectors and strings are checked before commit.
  Invalid input leaves the old image and descriptors intact. Cwd and descriptors
  survive exec, except `FD_CLOEXEC`/`O_CLOEXEC` descriptors. Up to 64 arguments,
  64 environment entries and 8192 bytes of combined strings are accepted.
- `fork`, `waitpid` (specific child or any child, with optional `WNOHANG`),
  process IDs and cooperative `sched_yield`. A fork copies memory, registers,
  heap state and cwd; inherited descriptors share open-file descriptions.
  Exited children retain wait status until reaped. User faults terminate only
  the affected process, currently with SIGSEGV status.
- There are three process slots. Scheduling happens on yield, blocking wait,
  pipe I/O that must wait, exit or a user exception; the child runs first after fork. This is not timer
  preemption. Each saved address space occupies 4 MiB, reserved at boot by the
  linker. Use **at least 64 MiB RAM** (the default VM has 256 MiB).
- The synchronous launch returns to Bssh when no runnable processes remain;
  Bssh's cwd is restored then. Background jobs, full init/orphan lifecycle and
  sessions are not yet supported. Unsupported operations fail explicitly.
  If all remaining processes are waiting with no runnable peer, the launcher
  reports the deadlock, closes their descriptors and returns status 125.
  No blocked processes survive that return to Bssh.

## Validation

```sh
make test-process-shell    # process probes through the normal ISO and ATA disk
make test-exec             # fork/wait/exec, environment, FD inheritance and failures
make test-pipe             # Linux/Barnix pipes, exec pipeline, normal ISO and deadlock recovery
make test-pipeline         # Bssh syntax, native sh comparison, pipes/redirections in the ISO
make test-linux            # same ELF on Linux and Barnix, plus fault recovery
make test-linux-commands   # all ten migrated command binaries on Linux
make test-linux-shell      # ./ launch, commands, legacy programs in the same VM
make test test-elf         # filesystem interoperability and ELF validation
```

Native tests require Linux i386 execution support; sandbox policies may block
these calls with SIGSYS. QEMU tests use temporary/RAM filesystems. Test payloads
and the selftest boot entry are linked only into `kernel-test.elf`, not the
normal kernel or ISO. The probe writes `linux-probe.dat` and exits with 37 on
success. To use it manually, install `userland/linux-probe.elf` into an offline
image with `scripts/install_apps.py`.

## Remaining work for Bash

Timer preemption, `clone`, general signal delivery/handlers, job control and
TTY/termios are still absent. A CPU-bound process cannot be preempted. Initial
Bssh launches have an empty environment; exec receives the environment passed
by its caller. TLS, complete libc startup support and memory allocation beyond
the bounded `brk` remain to be implemented. The user window is writable/executable
without per-segment permissions. FPU/SIMD context switching is not implemented;
current userland uses integer-only freestanding builds. `execve` currently accepts supported static ELF
only: scripts with shebangs, dynamic interpreters and set-ID execution are absent.
Longer pipelines, descriptor redirections, here-documents, command lists and
shell expansions are not implemented in Bssh. Terminal reads still block in
the kernel; pipeline sources should use files/input redirection when input is needed.
Named FIFOs, `poll/select`, asynchronous pipe I/O and pipe resizing are absent.

Ext2 still limits files to 268 KiB and the volume to 2 MiB. File descriptions
store absolute paths rather than inode references; rename/unlink semantics,
permissions, special files, symlinks and precise filesystem errno mapping need
further work. These limits prevent loading a normal Bash binary today.

## Removed obsolete code

`BarnixOld/` (the duplicate snapshot), the unused `kernel/` scaffold, the inactive
process-table stub, unused `ata.c/ata.h`, empty `io.c`, and old source versions
of the migrated commands were removed. Active drivers, remaining legacy tools,
custom application sources and persistent disk images were retained.

References: [ELF headers](https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.eheader.html),
[ELF program loading](https://refspecs.linuxfoundation.org/elf/gabi4+/ch5.pheader.html),
[Linux TSS layout](https://github.com/torvalds/linux/blob/master/arch/x86/include/asm/processor.h).

Process ABI reference: [Linux execve semantics](https://man7.org/linux/man-pages/man2/execve.2.html).
Pipe ABI references: [pipe/pipe2](https://man7.org/linux/man-pages/man2/pipe.2.html),
[pipe I/O and atomicity](https://man7.org/linux/man-pages/man7/pipe.7.html),
[readv/writev](https://man7.org/linux/man-pages/man2/readv.2.html).
Shell behavior references: [Bash pipelines](https://www.gnu.org/software/bash/manual/html_node/Pipelines.html),
[quoting](https://www.gnu.org/software/bash/manual/html_node/Quoting.html).
