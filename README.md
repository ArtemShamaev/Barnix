# Barnix

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
**live-ext2.img**. Burn the ISO **as a disc image** to CD-R/RW or DVD-R/RW and
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
- Regular files up to 255 bytes. Larger files, symlinks, extended attributes,
  inode flags and indirect block mappings are not modified or followed.
- Directories can use up to 12 direct blocks; navigation uses `cd name`,
  `cd ..`, and `cd /`. General paths and quoted names are not implemented.
- New names are limited to 23 characters. Existing longer names can be listed.
- The default image has 64 inodes, of which ext2 reserves 10 and `lost+found`
  occupies one: 53 are initially available for files and directories.

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
| `sync` | Confirm that the filesystem remains mounted; operations are synchronous |
| `help`, `clear`, `echo text`, `panic` | Help, screen, output, and halt test |

`cp` and `mv` reject existing destinations. Oversized writes and appends fail
without truncating the file. The shell accepts up to 127 characters per line.


## Tests

`make test` uses images created by `mke2fs`, imports Linux-side files through
`debugfs`, runs the real filesystem code with a sector-device mock, checks
exported contents, and runs `e2fsck -fn` after modifications. Tests cover both
ext2 revisions, both supported inode sizes, directory record variants, directory
growth, exhausted inodes, name/file limits, rollback, rejected unsupported
objects, corrupt headers/records, and injected disk-write failures.

`make test-shell` boots the ISO in QEMU on a temporary ext2 image, exercises
shell commands, checks persistence after restart, and verifies the resulting
image with `e2fsck` and `debugfs`. It also tests CD/DVD-only boot, volatile
RAM writes across reboots, Live-only isolation from an attached ATA disk, and
fallback with an unsupported disk. No test writes to the user's disk image.

`make test-repo` verifies the publication script against temporary local bare
Git repositories, including ignored artifacts, new branches, remote updates,
diverged history and rejected URLs. It never contacts GitHub.

Build dependencies: GCC/binutils, grub-mkrescue and xorriso; ext2 image creation
and tests require e2fsprogs (`mke2fs`, `debugfs`, `e2fsck`), Python 3 for tests,
and QEMU for `test-shell`.
