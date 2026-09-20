CC = gcc
LD = ld
OBJCOPY = objcopy

CFLAGS = -m32 -march=i386 -mno-sse -mno-mmx -msoft-float -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector -Wall -I.
LDFLAGS = -m elf_i386 -T linker.ld

OBJS = \
	boot.o \
	kernel.o \
	barnix.o \
	fs.o \
	disk.o \
	io.o \
	bssh.o \
	keyboard.o

DEPS = $(OBJS:.o=.d)
CFLAGS += -MMD -MP

.PHONY: all run run-cd clean distclean test test-shell test-repo
.DELETE_ON_ERROR:

$(OBJS): makefile

.DEFAULT_GOAL := all

ISO_DIR = iso
BOOT_DIR = $(ISO_DIR)/boot
GRUB_DIR = $(BOOT_DIR)/grub

all: barnix.iso

test:
	$(CC) -ffreestanding -fno-builtin -Wall -Wextra -I. tests/fs_test.c -o tests/fs_test
	python3 tests/ext2_test.py

test-shell: barnix.iso
	python3 tests/shell_test.py

test-repo:
	bash -n scripts/update-repo.sh
	python3 tests/update_repo_test.py

# =========================
# Compile
# =========================

boot.o: boot.S
	$(CC) $(CFLAGS) -c $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

barnix.o: barnix.c
	$(CC) $(CFLAGS) -c barnix.c -o barnix.o

fs.o: fs.c
	$(CC) $(CFLAGS) -c fs.c -o fs.o

disk.o: disk.c
	$(CC) $(CFLAGS) -c disk.c -o disk.o

io.o: io.c
	$(CC) $(CFLAGS) -c io.c -o io.o

bssh.o: bssh.c
	$(CC) $(CFLAGS) -c bssh.c -o bssh.o

keyboard.o: keyboard.c
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o

# =========================
# Kernel
# =========================

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJS)

# =========================
# HDD image
# =========================

barnix-ext2.img:
	dd if=/dev/zero of=barnix-ext2.img bs=1M count=2
	mke2fs -q -t ext2 -F -b 1024 -I 128 -N 64 -O none,filetype barnix-ext2.img

live-ext2.img:
	dd if=/dev/zero of=$@ bs=1M count=2
	mke2fs -q -t ext2 -F -b 1024 -I 128 -N 64 -O none,filetype $@

# =========================
# ISO
# =========================

barnix.iso: kernel.elf live-ext2.img grub.cfg
	mkdir -p $(GRUB_DIR)
	cp kernel.elf $(BOOT_DIR)/
	cp live-ext2.img $(BOOT_DIR)/
	cp grub.cfg $(GRUB_DIR)/
	grub-mkrescue -o barnix.iso $(ISO_DIR)

# =========================
# Run
# =========================

run: barnix.iso barnix-ext2.img
	qemu-system-i386 \
		-m 256M \
		-cdrom barnix.iso \
		-drive file=barnix-ext2.img,format=raw,if=ide

run-cd: barnix.iso
	qemu-system-i386 -m 256M -boot d -cdrom barnix.iso

# =========================
# Clean
# =========================

clean:
	rm -f *.o
	rm -f $(DEPS)
	rm -f tests/fs_test
	rm -f kernel.elf
	rm -f barnix.iso
	rm -f live-ext2.img
	rm -rf iso

distclean: clean
	rm -f barnix-ext2.img

-include $(DEPS)
