# Barnix
## Кароче:
Распакуешь скаченный репо (там около 1 гб)
В нём распакуй РАСПАКУЙ.zip
И так будет make-3.81.exe
и qemu-w64-setup_что-то-там.exe
Их запускаешь, по класике inosetup Далее, Далее и т.д.

Потом из папки Barnix в cmd
make run

Откроется Qemu. Там и будет Barnix. Ctrl + Alt + F откроется на полный экран.

Ну и там введи help (ну он не помещается) или смотри команды тута (сеть не работает)

| Command | Action |
| --- | --- |
| `ls`, `pwd` | List entries / print current directory |
| `mkdir name`, `cd name` | Create / enter a directory |
| `rmdir name` | Remove an empty directory |
| `touch name`, `cat name`, `rm name` | Create / read / remove a file |
| `write name text` | Replace file contents, creating the file if needed |
| `append name text` | Append text, creating the file if needed; no automatic newline |
| `cp source destination` | Copy a file to a new name in the current directory |
| `mv old new` | Rename a file or directory in the current directory |
| `stat name` | Display type and byte count; `stat .` inspects the current directory |
| `df` | Show free ext2 inodes, free blocks, and driver limits |
| `diskinfo` | Show ATA/RAM backend, device sectors, and driver sector limit |
| `devices` | List RAM, ATA, and USB mass-storage devices |
| `mount device /mountpoint` | Mount an ext2 device; currently only `/` is supported |
| `unmount` | Flush and unmount the current filesystem |
| `sync` | Confirm that the filesystem remains mounted; operations are synchronous |
| `./file.elf args` | Run a Barnix ELF32 application |
| `help`, `clear`, `echo text`, `panic`, `init` | Help, screen, output, halt test, and reload `/etc/sys-lang.cfg` |
| `macro file` | Full-screen UTF-8 text editor |


Barnix has its own kernel with experimental Linux i386 ABI support. Linux
ELF programs now launch through `./filename`; ten basic `/bin` commands use
Linux syscalls and ring 3. The Linux path now implements bounded cooperative
`fork/waitpid`, static-ELF `execve` with argument/environment passing, and
`pipe/pipe2` with blocking I/O between processes. See [Linux ABI status and tests](README-LINUX-REWRITE.md)
for the exact supported subset. Bash is not supported yet. Rebuild and reinstall
old Barnix applications: the legacy ABI now requires an explicit ELF marker.

Build with `make`; start with `make run` and select Barnix in GRUB.
Requires GCC with 32-bit freestanding support, binutils, grub-mkrescue,
xorriso, and QEMU for running the image.

## Publish changes to GitHub

```sh
./scripts/update-repo.sh "Describe your changes"
```

The script checks that `origin` points to `github.com/ArtemShamaev/Barnix`,
fetches the remote state, stages **all non-ignored changes**, commits them, and
pushes the current branch. Without a message it uses `Update Barnix`. With no
changes it pushes existing commits without creating an empty commit. It works
when called from another directory and requires Git authentication already set
up (HTTPS credential helper or SSH agent); no token is stored in the script.

It stops on detached HEAD, unfinished Git operations, or a remote branch that
is ahead/diverged. It never force-pushes or automatically resolves conflicts.
If the remote has advanced, commit/stash local work and integrate the remote
changes manually before running it again. See `--help` for usage.

## Boot from CD/DVD

`make` builds **barnix.iso**, including GRUB, the kernel and a separate clean
**live-ext2.img** containing all shell commands in `/bin`, plus `hello.elf` and `fileio.elf`. Burn the ISO **as a disc image** to CD-R/RW or DVD-R/RW and
select the optical drive in the computer's boot menu. BIOS/Legacy boot is
supported and tested in QEMU; UEFI boot of this VGA-text-mode kernel has not
been verified. The ISO uses GRUB's [bootable optical-disc support](https://www.gnu.org/software/grub/manual/grub/html_node/Making-a-GRUB-bootable-CD_002dROM.html).

GRUB offers two entries:

- **ATA disk or CD/DVD live fallback** (default): use a supported primary ATA
  ext2 disk if present; otherwise load the filesystem from the optical disc.
- **Live CD/DVD (RAM only)**: always use the bundled filesystem in memory,
  including when an ATA hard disk is attached.

In Live mode, file commands work but changes disappear on reboot. `sync`
confirms completion of RAM writes; it does not write to the CD/DVD or HDD.
The persistent `barnix-ext2.img` is not included in the ISO. GRUB reads the
optical medium and passes the ext2 image as a Multiboot module; the kernel
copies it into RAM before mounting. This does not add a general-purpose ATAPI
or ISO9660 driver for browsing other discs after boot.

```sh
make run-cd   # CD/DVD-only VM, no hard disk
make run      # ISO plus a persistent ATA ext2 disk
```

## Filesystem: ext2

Barnix now reads and writes real ext2 superblocks, group descriptors, allocation
bitmaps, inode tables, and directory entries. Images can be created with
`mke2fs` and inspected with `debugfs` and `e2fsck`.

`make` creates **barnix-ext2.img** when it is absent; `make run` attaches it as
the primary ATA disk. The old **barnix.img** is preserved. Its private Barnix
format is not ext2 and cannot be mounted by this driver; there is no automatic
conversion. Unknown, unsupported, or unclean images are rejected without
formatting them. If no supported ATA filesystem can be mounted, the kernel
loads the bundled ext2 filesystem from the CD/DVD into a writable RAM disk.

Supported profile:

- Raw filesystem at byte zero (no partition table), one block group, 1 KiB
  blocks, at most 2048 blocks / 2 MiB.
- Ext2 revision 0 or 1; 128-byte or 256-byte inodes.
- Optional `filetype`, `sparse_super`, and `large_file` feature flags.
  Other features, including journaling, extents, resize_inode, extended
  attributes and directory indexing, are not supported by this driver.
- Regular files up to **268 KiB (274432 bytes)**, using 12 direct blocks and
  one singly indirect block. Binary reads, sparse-file holes, copying, appending,
  truncation and removal are supported. Double/triple indirect blocks, symlinks,
  extended attributes and special inode flags are not supported.
- Directories can use up to 12 direct blocks; navigation uses `cd name`,
  `cd ..`, and `cd /`. Bssh supports quoted names, including spaces.
- New names are limited to 23 characters. Existing longer names can be listed.
- The default image has 64 inodes, of which ext2 reserves 10 and `lost+found`
  occupies one: 53 are available before importing programs, fewer after importing `/bin`, configuration and example executables. Existing compatible images retain
  their files and gain the larger per-file limit without reformatting.

Create a separate compatible image (use a new filename):

```sh
truncate -s 2M example-ext2.img
mke2fs -t ext2 -F -b 1024 -I 128 -N 64 -O none,filetype example-ext2.img
```

While Barnix is stopped, inspect its disk with:

```sh
e2fsck -fn barnix-ext2.img
debugfs -R 'ls -l /' barnix-ext2.img
debugfs -R 'cat /example' barnix-ext2.img
```

All successful changes are flushed immediately. Before writing, Barnix marks
the superblock unclean, and restores the clean flag only after the operation
completes. Logical failures (no space, invalid name, unsupported file) roll
back in memory without disk writes. A disk I/O failure disables further
operations; repair the unmounted image with `e2fsck` before rebooting. This is
not journaling and does not provide atomic recovery after power loss.
Barnix has no wall clock yet, so new inode timestamps use the volume's stored
last-write time.

The implementation follows the [Linux ext2 documentation](https://cdn.kernel.org/doc/html/latest/filesystems/ext2.html)
and is checked against e2fsprogs rather than relying only on its own reader.

## Cyrillic, keyboard layouts and system language

Barnix stores text as UTF-8 and renders Russian Cyrillic glyphs directly in
the VGA text console. File names, file contents, command arguments and
translated diagnostics can contain `А-Я`, `а-я`, `Ё` and `ё`. Invalid UTF-8 is
displayed as `?` without corrupting the input buffer. Backspace removes the
whole UTF-8 character.

The keyboard starts in the English layout. Press `Shift+Alt` to switch between
English and Russian; the shortcut is edge-triggered, so holding either key or
receiving a repeated make code does not toggle repeatedly. Caps Lock and Shift
work in both layouts. The VGA font is initialized during kernel startup and
contains the complete Russian alphabet plus `№`.

The system language is configured in `/etc/sys-lang.cfg`:

```text
LANG=ru
```

`LANG=en` selects English. The parser also accepts a bare `ru` or `en`, UTF-8
config BOMs, blank lines and `#` comments, but rejects multiple or unknown
settings without changing the current language. `init` is an ELF command in
`/bin`; it rereads this file and applies a valid change immediately. It does
not reboot and does not change the current working directory. Newly generated
images contain `/etc/sys-lang.cfg` with `LANG=en`.

## GCC ELF applications (legacy Barnix ABI)

Build and try the bundled examples in Live mode:

```sh
make apps
make run-cd
```

At the Barnix prompt:

```text
./hello.elf hello-from-barnix
./fileio.elf
stat program.dat
```

`hello.elf` prints its arguments, checks initialized data and zero-initialized
BSS, and returns status 7. `fileio.elf` writes and verifies a binary file of
32768 bytes through the kernel API and returns 0. Both return to the shell.
New live and USB images also contain copies under `/bin`. A bare command name
is looked up there and loaded by the ELF loader, so `hello.elf argument` and
`./bin/hello.elf argument` both execute the ELF file. Shell commands, including `help`, `cd`, `mount`, `init`, `macro` and `panic`, are
separate ELF executables in `/bin` (without an `.elf` suffix). The shell has no
built-in command handlers. Absolute and relative executable paths also work.
Ten basic commands now use Linux syscalls (see the ABI status above). Remaining
commands use kernel filesystem/device services through the Barnix ABI;
`cd` changes the shared working directory and persists after the program exits.
The loader caches validated boot copies of `mount`, `unmount` and `devices`
from the Live image so they remain executable after unmounting, device removal,
or switching to an older filesystem without `/bin`. Other missing commands
report an error; there is no built-in fallback.

Existing persistent images are preserved. With QEMU stopped, run
`make install-apps` to install/update `/bin` in `barnix-ext2.img`. Installation
validates a temporary copy before replacing the image, preserving other files.
For one custom program, use `make add-programm apps/calc.elf`; the target also
builds `apps/calc.elf` from `apps/calc.c` when it is missing or stale. The
program is installed as both `/bin/calc.elf` and `/calc.elf`, with executable
permissions. `make add-programm PROGRAM=apps/calc.elf` is an equivalent form.
Back up important disk images before updating them. To update another offline
image, use `python3 scripts/install_apps.py IMAGE apps/bin/* apps/hello.elf apps/fileio.elf`.
Booting an old image before installing commands still permits `mount ram0 /`
to switch to the bundled Live filesystem.
The examples are included in every newly built Live image and newly created
ATA image. An existing `barnix-ext2.img` is never overwritten by a rebuild.
To import the examples into that image, stop QEMU first, then run (the target
names must not already exist):

```sh
debugfs -w -R 'write apps/hello.elf /hello.elf' barnix-ext2.img
debugfs -w -R 'write apps/fileio.elf /fileio.elf' barnix-ext2.img
e2fsck -fn barnix-ext2.img
```

Create `apps/myprog.c`, for example:

```c
#include "barnix_app.h"

int main(int argc, const char *const *argv)
{
    barnix->puts("My GCC program is running");
    if (argc > 1) barnix->puts(argv[1]);
    return 0;
}
```

Compile it with `make apps/myprog.elf` from the project root, or run `make
myprog.elf` inside `apps/`; the `.elf` file is produced from `myprog.c`. Then
import it into the stopped ATA
image with `debugfs -w -R 'write apps/myprog.elf /myprog.elf' barnix-ext2.img`.
Run `./myprog.elf argument` in Barnix. Add it to `APPS` in the makefile to
include it in the Live ISO instead.

The build uses GCC `-m32 -march=i386 -ffreestanding`, disables PIE, stack
protectors and floating-point/SIMD instructions, and statically links with
`apps/app.ld`. `apps/start.c` adapts the entry point to C `main`; `runtime.c`
supplies GCC's basic memory helpers. This is the **Barnix ABI**, not Linux:
ordinary Linux executables using glibc, Linux syscalls, a dynamic interpreter,
TLS or PIE cannot be run. There is no libc, heap allocator or C++ runtime.
Compiler helpers for operations such as 64-bit division require a suitable
32-bit freestanding library supplied by the application build.

The shared `app_abi.h` defines API version 5: binary file I/O, console output,
filesystem operations, directory state, mount management and device queries.
It also provides configuration reload, localized message lookup, raw keyboard
events and VGA cursor positioning for full-screen applications such as `macro`.
Applications must be rebuilt against this version. The original prefix
(`puts`, `size`, `read`, `write`, `append`) remains in place. Names resolve in the shell's current directory. `read` returns
bytes read (including zero at EOF), `size` returns byte length, mutations return
0 on success; errors return -1. File APIs preserve embedded zero bytes.

The legacy execution path accepts static little-endian ELF32 i386 `ET_EXEC` files. It
validates all load ranges and the executable entry point before copying any
segment, zeroes BSS, and rejects overlaps, invalid lengths, out-of-bounds loads,
dynamic linking and TLS. It reserves 16–20 MiB for one program, with the last
64 KiB used as a separate stack. The linker reserves this memory so GRUB modules
cannot overlap it. Use at least 64 MiB of RAM (the default VM uses 256 MiB).

Programs run synchronously at **ring 0**, without paging or process isolation.
An invalid memory access, infinite loop or privileged instruction can hang or
crash Barnix; only run trusted programs built for this ABI. A normal return from
`main` restores the kernel stack and reports the exit status in the shell.
This legacy execution path has no scheduler or recovery from application faults.
The separate experimental Linux ABI path is described above.

Format and compiler references: [ELF program loading](https://gabi.xinuos.com/v42/elf/07-pheader.html)
and [GCC link options](https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html).

## Shell commands

| Command | Action |
| --- | --- |
| `ls`, `pwd` | List entries / print current directory |
| `mkdir name`, `cd name` | Create / enter a directory |
| `rmdir name` | Remove an empty directory |
| `touch name`, `cat name`, `rm name` | Create / read / remove a file |
| `write name text` | Replace file contents, creating the file if needed |
| `append name text` | Append text, creating the file if needed; no automatic newline |
| `cp source destination` | Copy a file to a new name in the current directory |
| `mv old new` | Rename a file or directory in the current directory |
| `stat name` | Display type and byte count; `stat .` inspects the current directory |
| `df` | Show free ext2 inodes, free blocks, and driver limits |
| `diskinfo` | Show ATA/RAM backend, device sectors, and driver sector limit |
| `devices` | List RAM, ATA, and USB mass-storage devices |
| `mount device /mountpoint` | Mount an ext2 device; currently only `/` is supported |
| `unmount` | Flush and unmount the current filesystem |
| `sync` | Confirm that the filesystem remains mounted; operations are synchronous |
| `./file.elf args` | Run a Barnix ELF32 application |
| `help`, `clear`, `echo text`, `panic`, `init` | Help, screen, output, halt test, and reload `/etc/sys-lang.cfg` |
| `macro file` | Full-screen UTF-8 text editor |

`cp` and `mv` reject existing destinations. Oversized writes and appends fail
without truncating the file. The shell accepts up to 127 characters per line.

Bssh supports a foreground pipeline of **two Linux commands**, plus input,
output and append redirection:

```text
echo hello | cat
echo first > result
echo second >> result
cat < result | cat > copy
echo "literal | > and spaces" > "two words"
```

Operators also work without spaces. Single/double quotes and backslash escapes
preserve literal arguments; adjacent quoted and unquoted text forms one argument.
Redirections are applied left to right, after pipe connections. The shell waits
for both commands and reports the last command's exit status. Syntax errors are
rejected before execution. There are at most eight redirections per command.

Pipelines and redirections require Linux programs. Legacy tools such as `cd`,
`help`, and the editor still work as standalone commands. Bssh does not support
longer pipelines, `&&`, `||`, background jobs, descriptor syntax such as `2>`,
here-documents, or expansions of variables, wildcards or command substitutions.
Terminal reads are still synchronous; use files or input redirection for pipe
sources that need input. This remains a small shell, not Bash.

`macro file` opens the full-screen Barnino System(s) Macro text editor. The
top line identifies the editor and the next line shows the file. The editor
supports UTF-8 text, Russian and English keyboard layouts, cursor movement
with the arrow keys, Backspace/Delete, and scrolling through the file. Its
bottom bar always shows the shortcuts: `Ctrl+S` saves, `Ctrl+Q` exits, and
`Ctrl+F` searches forward from the cursor. Saving writes the complete buffer
back to the file and keeps the editor open.

The shell completes a unique command when `Tab` is pressed. `Up` and `Down`
recall the last 16 non-empty commands; repeated adjacent commands are stored
only once. Completion and history are available in both keyboard layouts.

`barnixiolib/stdio.h` and `barnixiolib/stdio.c` provide a small stdio-like
library for Barnix ELF applications. It includes `putchar`, `puts`, `printf`,
`getchar`, `scanf`, `fopen`, `fclose`, `fread` and `fwrite`, backed by the Barnix ABI rather than
Linux file descriptors. Application targets link it automatically; include
`barnixiolib/stdio.h` when writing a program for this environment; the build
also maps ordinary `#include <stdio.h>` to this Barnix header. For example,
save `calc.c` under `apps/` and run `make apps/calc.elf`; this supplies the
freestanding flags, startup code and Barnix I/O library. The headers use paths
relative to the project, so they also work when included by an application
compiled from inside `apps/`.

Command-line arguments are available through `argument_count()`,
`argument_value(index)` and `argument_int(index, &value)` in the same header.
`argument_value(0)` is the name used to launch the program; invalid indexes
return `NULL`, and `argument_int` returns `0` only for a valid complete integer.

## USB flash drives

Barnix supports one directly attached full-speed USB Mass Storage device using
the UHCI controller and USB Mass Storage Bulk-Only Transport. The device must
contain the supported ext2 profile either as a raw filesystem or as the first
Linux-type (`0x83`) primary MBR partition. EHCI/xHCI, hubs, composite devices,
USB keyboards, and FAT/exFAT/NTFS are not supported yet.

In QEMU, attach a flash image with a UHCI controller:

```sh
qemu-system-i386 -cdrom barnix.iso -device piix3-usb-uhci,id=uhci \
  -drive if=none,id=flashfile,file=flash.img,format=raw \
  -device usb-storage,bus=uhci.0,drive=flashfile,id=flash
```

Then use `devices` and `mount usb0 /`. The mount operation validates ext2 before
switching the active root; it never formats an unknown device. `unmount`
flushes the filesystem first, then clears the active device. If the USB device
is removed while mounted, the next filesystem operation detects the removal,
invalidates the cached filesystem, and refuses further I/O until another
device is mounted. Reconnecting a flash drive performs a new USB enumeration.

`mount ata0 /` and `mount ram0 /` switch back to the ATA or Live RAM filesystem.
Only one filesystem is active at `/` at a time. A mounted USB filesystem uses
the same ext2 limits as ATA: 1 KiB blocks, up to 268 KiB per regular file, and
up to 12 direct plus one singly indirect block. Changes are flushed through
SCSI READ(10)/WRITE(10) and SYNCHRONIZE CACHE commands.

Create a fresh virtual flash drive and automatically format it as ext2:

```sh
make usb-disk                         # creates barnix-usb.img
python3 scripts/make_usb_disk.py usb.img apps/hello.elf apps/fileio.elf
make run_usb                          # build and boot with barnix-usb.img
```

With no program arguments, the script imports `apps/*.elf` and `apps/bin/*`. The
image is a 2 MiB raw ext2 filesystem, matching the current Barnix disk limit.
An existing output is protected; pass `--force` when it is intentional to
reformat it. Attach `barnix-usb.img` to QEMU using the USB example above, then
run `devices` and `mount usb0 /` in the shell.

`make run_usb` starts QEMU with the UHCI controller and attaches the generated
image as a USB Mass Storage device. Set another image with
`make run_usb USB_DISK=/path/to/flash.img`.


## Tests

`make test` uses images created by `mke2fs`, imports Linux-side files through
`debugfs`, runs the real filesystem code with a sector-device mock, checks
exported contents, and runs `e2fsck -fn` after modifications. Tests cover both
ext2 revisions, both supported inode sizes, directory record variants, directory
growth, exhausted inodes/blocks, binary files up to 268 KiB, indirect blocks,
sparse files, truncation, name/file limits, rollback, rejected unsupported
objects, corrupt headers/records, and injected disk-write failures.

`make test-shell` boots the ISO in QEMU on a temporary ext2 image, exercises
shell commands, checks persistence after restart, and verifies the resulting
image with `e2fsck` and `debugfs`. It also tests CD/DVD-only boot, volatile
RAM writes across reboots, Live-only isolation from an attached ATA disk, and
fallback with an unsupported disk. GCC ELF examples are run from both Live and
ATA filesystems, checking arguments, return status, repeat loading/BSS clearing,
copying executables, and 32 KiB binary file I/O across reboot. No test writes to
the user's disk image.

`make test-install` verifies repeated offline installation, preservation of user
files and rollback on failed imports.

`make test-pipeline` tests quoting and syntax bounds, compares the Linux runner
with `sh` for supported pipelines/redirections, then exercises Bssh through
QEMU keyboard input. It checks binary output, last-command status, invalid
syntax, legacy rejection, process faults, cwd, repeated launches and live boot.

`make test-elf` tests the ELF validator with real GCC output and malformed
headers/segments, including out-of-bounds loads, entry points, overlap,
truncation, wrong architectures, PIE, TLS, and dynamic loading.

`make test-usb` boots QEMU with a USB Mass Storage image, tests enumeration,
raw and MBR-partitioned ext2, mounting, writing, ELF execution, unmounting,
reconnection, removal detection, and e2fsck validation.

`make test-lang` boots QEMU and verifies Cyrillic input/output, UTF-8 names and
contents, Shift+Alt layout switching, boot-time language selection and `init`.
`make test-text` covers the keyboard decoder, UTF-8 validation and config
parser without QEMU.

`make test-repo` verifies the publication script against temporary local bare
Git repositories, including ignored artifacts, new branches, remote updates,
diverged history and rejected URLs. It never contacts GitHub.

Build dependencies: GCC/binutils, grub-mkrescue and xorriso; ext2 image creation
and tests require e2fsprogs (`mke2fs`, `debugfs`, `e2fsck`), Python 3 for tests,
and QEMU for `test-shell`.

### ELF compatibility

The launcher selects Linux ABI for supported System V/Linux ELF32 i386 files
and legacy Barnix ABI for rebuilt files marked with standalone OSABI 255,
version 5. Linux loading supports standard virtual addresses such as
`0x08048000` within a bounded 4 MiB window. Dynamic linking, TLS, PIE and ELF64
remain unsupported. See [the compatibility status](README-LINUX-REWRITE.md)
for limits and tests. The migrated commands are freestanding Linux programs;
ordinary Bash, GCC and coreutils binaries still need further kernel support.
