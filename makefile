CC = gcc
LD = ld
OBJCOPY = objcopy

CFLAGS = -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector -Wall -I.
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

ISO_DIR = iso
BOOT_DIR = $(ISO_DIR)/boot
GRUB_DIR = $(BOOT_DIR)/grub

all: barnix.iso

# =========================
# Compile
# =========================

boot.o: boot.S
	$(CC) $(CFLAGS) -c boot.S -o boot.o

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

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJS)

# =========================
# HDD image
# =========================

barnix.img:
	dd if=/dev/zero of=barnix.img bs=1M count=32

fs.img:
	dd if=/dev/zero of=fs.img bs=512 count=4096

# =========================
# ISO
# =========================

barnix.iso: kernel.elf fs.img barnix.img grub.cfg
	mkdir -p $(GRUB_DIR)
	cp kernel.elf $(BOOT_DIR)/
	cp fs.img $(BOOT_DIR)/
	cp grub.cfg $(GRUB_DIR)/
	grub-mkrescue -o barnix.iso $(ISO_DIR)

# =========================
# Run
# =========================

run: barnix.iso
	qemu-system-i386 \
		-m 256M \
		-cdrom barnix.iso \
		-drive file=barnix.img,format=raw,if=ide

# =========================
# Clean
# =========================

clean:
	rm -f *.o
	rm -f kernel.elf
	rm -f barnix.iso
	rm -f fs.img
	rm -rf iso

distclean: clean
	rm -f barnix.img
