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
	elf.o \
	elf_format.o \
	elf_enter.o \
	disk.o \
	storage_pci.o \
	net.o \
	net_e1000.o \
	net_pcnet.o \
	net_ip.o \
	linux_task.o \
	linux_memory.o \
	linux_exec.o \
	linux_syscall.o \
	linux_idt.o \
	linux_int80.o \
	linux_tss.o \
	linux_user.o \
	usb_storage.o \
	usb_hid.o \
	usb_xhci.o \
	usb_legacy.o \
	bssh.o \
	bssh_runner.o \
	shell_parse.o \
	keyboard.o \
	mouse.o \
	text.o \
	font.o \
	console_video.o \
	settings.o \
	lang.o

DEPS = $(OBJS:.o=.d)
CFLAGS += -MMD -MP

.PHONY: all rebuild apps usb-disk run run-net run-net-pcnet run-cd run_usb clean distclean test test-elf test-shell test-usb test-repo test-text test-lang test-install run-setup
.DELETE_ON_ERROR:

$(OBJS): makefile

.DEFAULT_GOAL := all

ISO_DIR = iso
BOOT_DIR = $(ISO_DIR)/boot
GRUB_DIR = $(BOOT_DIR)/grub

all: barnix.iso setup

rebuild:
	$(MAKE) clean
	$(MAKE) all

APP_CFLAGS = -m32 -march=i386 -mno-sse -mno-mmx -msoft-float -O2 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -Wall -Wextra -I. -Iapps -Ibarnixiolib
LINUX_COMMANDS = echo pwd cat ls touch write append cp true false
COMMANDS = df diskinfo devices clear rm mkdir cd rmdir stat mv panic sync mount format unmount help init macro bnm get git chmod su sudo whoami useradd theme snake pong fdisk paint execute bcc
APPS = apps/hello.elf apps/fileio.elf apps/theme.elf apps/snake.elf apps/asciiart.elf apps/excuse.elf apps/fortune.elf apps/pong.elf $(addprefix apps/bin/,$(sort $(COMMANDS) $(LINUX_COMMANDS)))

apps: $(APPS)

apps/%.elf: apps/%.c apps/start.c apps/runtime.c text.c barnixiolib/stdio.c barnixiolib/stdio.h apps/barnix_app.h app_abi.h apps/app.ld scripts/mark_barnix_elf.py makefile
	$(CC) $(APP_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c text.c barnixiolib/stdio.c $< -o $@
	python3 scripts/mark_barnix_elf.py $@

apps/bin/%: apps/%.c apps/command.h apps/start.c apps/runtime.c text.c barnixiolib/stdio.c barnixiolib/stdio.h apps/barnix_app.h app_abi.h apps/app.ld scripts/mark_barnix_elf.py makefile
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c text.c barnixiolib/stdio.c $< -o $@
	python3 scripts/mark_barnix_elf.py $@

# Execute shares the SDK graphics implementation, without a packaged SDK dependency.
apps/bin/execute: apps/execute.c apps/start.c apps/runtime.c text.c BDK/runtime/graphics.c BDK/include/barnix_graphics.h BDK/include/barnix_api.h apps/barnix_app.h app_abi.h apps/app.ld makefile
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c text.c BDK/runtime/graphics.c apps/execute.c -o $@
	python3 scripts/mark_barnix_elf.py $@

apps/bin/bcc: apps/bcc.c apps/bcc_template.inc apps/start.c apps/runtime.c text.c barnixiolib/stdio.c barnixiolib/stdio.h apps/barnix_app.h app_abi.h apps/app.ld makefile
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c text.c barnixiolib/stdio.c apps/bcc.c -o $@
	python3 scripts/mark_barnix_elf.py $@

apps/bin/%.elf: apps/%.elf makefile
	mkdir -p apps/bin
	cp $< $@

USER_CFLAGS = -m32 -march=i386 -mno-sse -mno-mmx -msoft-float -O2 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -Wall -Wextra

$(addprefix apps/bin/,$(LINUX_COMMANDS)): apps/bin/%: userland/commands.c userland/start.S userland/syscall.h include/uapi/unistd.h makefile
	mkdir -p apps/bin
	$(CC) $(USER_CFLAGS) -DCOMMAND='"$*"' -nostdlib -static -no-pie -Wl,--build-id=none userland/start.S userland/commands.c -o $@

USB_DISK ?= barnix-usb.img
NET_MODEL ?= e1000
QEMU_NET = -netdev user,id=net0 -device $(NET_MODEL),netdev=net0

usb-disk: apps scripts/make_usb_disk.py
	python3 scripts/make_usb_disk.py --force $(USB_DISK) $(APPS)

test-elf: apps
	$(CC) -Wall -Wextra -I. tests/elf_test.c -o tests/elf_test
	./tests/elf_test $(APPS)

test:
	$(CC) -ffreestanding -fno-builtin -Wall -Wextra -I. tests/fs_test.c -o tests/fs_test
	python3 tests/ext2_test.py

test-shell: barnix.iso
	python3 tests/shell_test.py

test-usb: barnix.iso
	python3 tests/usb_test.py

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

net.o: net.c
	$(CC) $(CFLAGS) -c net.c -o net.o

net_e1000.o: net_e1000.c net.h
	$(CC) $(CFLAGS) -c net_e1000.c -o net_e1000.o

net_pcnet.o: net_pcnet.c net.h
	$(CC) $(CFLAGS) -c net_pcnet.c -o net_pcnet.o

net_ip.o: net_ip.c net.h
	$(CC) $(CFLAGS) -c net_ip.c -o net_ip.o

linux_syscall.o: linux_syscall.c linux_abi.h
	$(CC) $(CFLAGS) -c linux_syscall.c -o linux_syscall.o

linux_idt.o: linux_idt.c linux_idt.h linux_abi.h
	$(CC) $(CFLAGS) -c linux_idt.c -o linux_idt.o

linux_int80.o: linux_int80.S
	$(CC) $(CFLAGS) -c linux_int80.S -o linux_int80.o

linux_tss.o: linux_tss.c linux_tss.h
	$(CC) $(CFLAGS) -c linux_tss.c -o linux_tss.o

linux_user.o: linux_user.S linux_user.h
	$(CC) $(CFLAGS) -c linux_user.S -o linux_user.o

usb_storage.o: usb_storage.c
	$(CC) $(CFLAGS) -c $< -o $@

bssh.o: bssh.c
	$(CC) $(CFLAGS) -c bssh.c -o bssh.o

bssh_runner.o: bssh_runner.S userland/bssh-run.elf
	$(CC) $(CFLAGS) -c $< -o $@

userland/bssh-run.elf: userland/bssh-run.c userland/start.S userland/syscall.h shell_parse.c shell_parse.h include/uapi/unistd.h makefile
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-Ttext-segment=0x0b000000 userland/start.S userland/bssh-run.c shell_parse.c -o $@

keyboard.o: keyboard.c
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o

elf.o: elf.c
	$(CC) $(CFLAGS) -c $< -o $@

elf_format.o: elf_format.c
	$(CC) $(CFLAGS) -c $< -o $@

elf_enter.o: elf_enter.S
	$(CC) $(CFLAGS) -c $< -o $@

# =========================
# Kernel
# =========================

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJS)

# =========================
# HDD image
# =========================

barnix-ext2.img: | live-ext2.img
	cp live-ext2.img $@

live-ext2.img: $(APPS) scripts/make_live_image.py scripts/install_apps.py
	python3 scripts/make_live_image.py $@ $(APPS)

# =========================
# ISO
# =========================

barnix.iso: kernel.elf live-ext2.img grub.cfg
	mkdir -p $(GRUB_DIR)
	cp kernel.elf $(BOOT_DIR)/
	cp live-ext2.img $(BOOT_DIR)/
	cp grub.cfg $(GRUB_DIR)/
	grub-mkrescue -d /usr/lib/grub/i386-pc -o barnix.iso $(ISO_DIR)

# =========================
# Run
# =========================

ifeq ($(FIRMWARE),efi)
run: run-installed
else
run: barnix.iso barnix-ext2.img
	python3 scripts/managed_disks.py run \
		-m 256M \
		-boot d \
		-drive file=barnix.iso,media=cdrom,if=ide,index=3 \
		-drive file=barnix-ext2.img,format=raw,if=ide \
		$(QEMU_NET)

endif

run-net: barnix.iso barnix-ext2.img
	python3 scripts/managed_disks.py run -m 256M -boot d -drive file=barnix.iso,media=cdrom,if=ide,index=3 \
		-drive file=barnix-ext2.img,format=raw,if=ide \
		$(QEMU_NET)

run-net-pcnet: barnix.iso barnix-ext2.img
	python3 scripts/managed_disks.py run -m 256M -boot d -drive file=barnix.iso,media=cdrom,if=ide,index=3 \
		-drive file=barnix-ext2.img,format=raw,if=ide \
		-netdev user,id=net0 -device pcnet,netdev=net0

run-cd: barnix.iso
	python3 scripts/managed_disks.py run -m 256M -boot d -drive file=barnix.iso,media=cdrom,if=ide,index=3 $(QEMU_NET)

run_usb: barnix.iso apps
	@if test ! -f "$(USB_DISK)"; then \
		python3 scripts/make_usb_disk.py "$(USB_DISK)" $(APPS); \
	fi
	python3 scripts/managed_disks.py run \
		-m 256M -boot d \
		-drive file=barnix.iso,media=cdrom,if=ide,index=3 \
		$(QEMU_NET) \
		-device piix3-usb-uhci,id=uhci \
		-drive if=none,id=flashfile,file=$(USB_DISK),format=raw \
		-device usb-storage,bus=uhci.0,drive=flashfile,id=flash

# =========================
# Clean
# =========================

clean:
	rm -f *.o
	rm -f $(DEPS)
	rm -f tests/fs_test tests/vfs_test tests/text_test tests/shell_parse_test
	rm -f tests/elf_test $(APPS)
	rm -f kernel.elf kernel-test.elf kernel-test.d linux_probe.d userland/linux-probe.elf userland/exec-parent.elf userland/exec-child.elf userland/fork-probe.elf
	rm -f tests/linux_selftest.o tests/linux_selftest.d
	rm -f userland/bssh-run.elf userland/pipe-probe.elf
	rm -f barnix.iso barnix-legacy-setup.iso barnix-efi-setup.iso kernel-efi.elf kernel-efi.d boot-efi.d
	rm -f Setup/*.o Setup/*.d Setup/kernel.elf Setup/kernel-efi.elf
	rm -f live-ext2.img
	rm -rf iso
	rm -rf Setup/build

distclean: clean
	rm -f barnix-ext2.img

-include $(DEPS)

.PHONY: install-apps add-programm add-program
install-apps: apps
	python3 scripts/install_apps.py barnix-ext2.img $(APPS)

# Install one or more user-built programs. The positional form is intentional:
# `make add-programm apps/calc.elf` first builds the ELF target, then imports it.
PROGRAM_GOALS = $(filter apps/%.elf,$(MAKECMDGOALS))
PROGRAM_GOALS += $(PROGRAM)
add-programm: $(PROGRAM_GOALS) barnix-ext2.img
	@test -n "$(strip $(PROGRAM_GOALS))" || (echo "usage: make add-programm apps/program.elf"; exit 2)
	python3 scripts/install_apps.py barnix-ext2.img $(PROGRAM_GOALS)
add-program: add-programm

.PHONY: test-install
test-install: apps
	python3 tests/install_test.py

.PHONY: test-text test-lang
test-text:
	$(CC) -fno-builtin -Wall -Wextra -I. tests/text_test.c text.c keyboard.c mouse.c lang.c -o tests/text_test
	./tests/text_test

test-lang: barnix.iso
	python3 tests/lang_test.py

userland/linux-probe.elf: userland/linux-probe.S userland/linux-file-probe.c
	$(CC) $(APP_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none userland/linux-probe.S userland/linux-file-probe.c -o $@

linux_probe.o: linux_probe.S userland/linux-probe.elf
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-linux
test-linux: kernel-test.elf live-ext2.img
	python3 tests/linux_test.py

.PHONY: test-linux-shell
test-linux-shell: barnix.iso userland/linux-probe.elf
	python3 tests/linux_shell_test.py

.PHONY: test-linux-commands
test-linux-commands: apps
	python3 tests/linux_commands_test.py

# The normal kernel contains neither the ABI probe nor its test boot entry.
kernel-test.o: kernel.c makefile
	$(CC) $(CFLAGS) -DBARNIX_LINUX_SELFTEST -c $< -o $@

tests/linux_selftest.o: tests/linux_selftest.c linux_exec.h fs.h barnix.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel-test.elf: $(filter-out kernel.o,$(OBJS)) kernel-test.o tests/linux_selftest.o linux_probe.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(filter-out kernel.o,$(OBJS)) kernel-test.o tests/linux_selftest.o linux_probe.o

-include kernel-test.d tests/linux_selftest.d linux_probe.d

userland/exec-parent.elf: userland/exec-parent.c userland/start.S userland/syscall.h
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none userland/start.S $< -o $@

userland/exec-child.elf: userland/exec-child.c userland/start.S userland/syscall.h
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-Ttext-segment=0x09000000 userland/start.S $< -o $@

.PHONY: test-exec
test-exec: kernel-test.elf live-ext2.img userland/exec-parent.elf userland/exec-child.elf userland/fork-probe.elf
	python3 tests/exec_test.py

userland/fork-probe.elf: userland/fork-probe.c userland/start.S userland/syscall.h include/uapi/unistd.h
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none userland/start.S $< -o $@

.PHONY: test-process-shell
test-process-shell: barnix.iso userland/exec-parent.elf userland/exec-child.elf userland/fork-probe.elf
	python3 tests/process_shell_test.py

userland/pipe-probe.elf: userland/pipe-probe.c userland/start.S userland/syscall.h include/uapi/unistd.h makefile
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-Ttext-segment=0x0a000000 userland/start.S $< -o $@

.PHONY: test-pipe
test-pipe: kernel-test.elf barnix.iso userland/pipe-probe.elf
	python3 tests/pipe_test.py

.PHONY: test-shell-parse test-pipeline
test-shell-parse:
	$(CC) -Wall -Wextra -Werror -I. tests/shell_parse_test.c shell_parse.c -o tests/shell_parse_test
	./tests/shell_parse_test

test-pipeline: test-shell-parse barnix.iso userland/linux-probe.elf
	python3 tests/pipeline_test.py

# Positional image name: make img work (also accepts NAME=work).
ifneq ($(filter img,$(MAKECMDGOALS)),)
export IMG_SIZE_MIB := $(if $(SIZE_MIB),$(SIZE_MIB),2)
export IMG_REQUEST := $(if $(NAME),$(NAME),$(filter-out img,$(MAKECMDGOALS)))
IMG_POSITIONAL := $(filter-out img,$(MAKECMDGOALS))
ifneq ($(strip $(IMG_POSITIONAL)),)
.PHONY: $(IMG_POSITIONAL)
$(IMG_POSITIONAL): img
	@:
endif
endif
.PHONY: img
img:
	python3 scripts/managed_disks.py create

.PHONY: test-vfs
test-vfs:
	$(CC) -ffreestanding -fno-builtin -Wall -Wextra -I. tests/vfs_test.c -o tests/vfs_test
	python3 tests/vfs_test.py

.PHONY: test-disks
test-disks: barnix.iso
	python3 tests/disks_test.py

.PHONY: test-img
test-img:
	python3 tests/managed_disks_test.py

.PHONY: test-font
test-font:
	$(CC) -Wall -Wextra -I. tests/font_test.c text.c -o /tmp/barnix-font-test
	/tmp/barnix-font-test

# Separate LiveCD installer; the regular kernel does not contain the wizard.
Setup/kernel.o: kernel.c Setup/setup.h makefile
	$(CC) $(CFLAGS) -DBARNIX_SETUP -c $< -o $@
Setup/build/profiles.h: Setup/profiles.json Setup/build.py
	python3 Setup/build.py --header
Setup/wizard.o: Setup/wizard.c Setup/setup.h Setup/build/profiles.h
	$(CC) $(CFLAGS) -c $< -o $@
Setup/kernel.elf: $(filter-out kernel.o,$(OBJS)) Setup/kernel.o Setup/wizard.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(filter-out kernel.o,$(OBJS)) Setup/kernel.o Setup/wizard.o
.PHONY: setup setup-build run-setup test-setup
FIRMWARE ?= legacy
export SETUP_DISK
setup: setup-build
SETUP_ISOS = barnix-legacy-setup.iso barnix-efi-setup.iso
setup-build: $(SETUP_ISOS)
$(SETUP_ISOS) &: kernel.elf kernel-efi.elf live-ext2.img Setup/kernel.elf Setup/kernel-efi.elf Setup/build.py Setup/profiles.json scripts/install_apps.py scripts/add_elf.py $(wildcard Setup/extra-apps Setup/extra-apps/*.elf)
	python3 Setup/build.py
run-setup: setup-build
	python3 Setup/run.py --firmware $(FIRMWARE)
.PHONY: run-installed test-setup-launcher
run-installed:
	python3 Setup/run.py --installed --firmware $(FIRMWARE)
test-setup-launcher:
	python3 tests/setup_launcher_test.py
test-setup: setup-build
	python3 Setup/test_setup.py
-include Setup/kernel.d Setup/wizard.d

.PHONY: test-large-fs
test-large-fs:
	$(CC) -O2 -ffreestanding -fno-builtin -Wall -Wextra -I. tests/large_fs_test.c -o tests/large_fs_test
	python3 tests/large_fs_test.py

.PHONY: test-storage-pci
test-storage-pci: barnix.iso
	python3 tests/storage_pci_test.py

.PHONY: test-setup-storage
test-setup-storage: setup-build
	python3 Setup/test_storage.py

# UEFI keeps the 32-bit kernel ABI; GRUB supplies a GOP framebuffer.
boot-efi.o: boot.S makefile
	$(CC) $(CFLAGS) -DBARNIX_EFI -c $< -o $@
kernel-efi.o: kernel.c makefile
	$(CC) $(CFLAGS) -DBARNIX_EFI -c $< -o $@
kernel-efi.elf: $(filter-out kernel.o boot.o,$(OBJS)) boot-efi.o kernel-efi.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(filter-out kernel.o boot.o,$(OBJS)) boot-efi.o kernel-efi.o
Setup/kernel-efi.o: kernel.c Setup/setup.h makefile
	$(CC) $(CFLAGS) -DBARNIX_SETUP -DBARNIX_EFI -c $< -o $@
Setup/wizard-efi.o: Setup/wizard.c Setup/setup.h Setup/build/profiles.h
	$(CC) $(CFLAGS) -DBARNIX_EFI -c $< -o $@
Setup/kernel-efi.elf: $(filter-out kernel.o boot.o,$(OBJS)) boot-efi.o Setup/kernel-efi.o Setup/wizard-efi.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(filter-out kernel.o boot.o,$(OBJS)) boot-efi.o Setup/kernel-efi.o Setup/wizard-efi.o
-include boot-efi.d kernel-efi.d Setup/kernel-efi.d Setup/wizard-efi.d

.PHONY: test-setup-efi
test-setup-efi: setup-build
	python3 Setup/test_efi.py

.PHONY: test-permissions
test-permissions:
	$(CC) -O2 -ffreestanding -fno-builtin -Wall -Wextra -I. tests/permissions_test.c -o tests/permissions_test
	python3 tests/permissions_test.py

.PHONY: test-system-permissions
test-system-permissions: all
	python3 tests/system_permissions.py

.PHONY: test-partitions
test-partitions: barnix.iso
	python3 tests/partition_shell_test.py

.PHONY: test-mouse
test-mouse:
	$(CC) -Wall -Wextra -I. tests/mouse_test.c mouse.c -o /tmp/barnix-mouse-test
	/tmp/barnix-mouse-test

.PHONY: test-mouse-paint
test-mouse-paint: all
	python3 tests/mouse_paint_test.py

.PHONY: test-usb-mouse
test-usb-mouse: all
	python3 tests/mouse_paint_test.py --usb

.PHONY: bdk test-bdk test-bdk-qemu
bdk:
	python3 BDK/package.py

test-bdk: bdk apps/hello.elf
	python3 tests/bdk_test.py

test-bdk-qemu: test-bdk barnix.iso
	python3 tests/bdk_qemu_test.py

.PHONY: test-bdk-setup
test-bdk-setup: bdk
	python3 tests/bdk_setup_test.py

# Import an existing ELF; positional arguments are not independent make targets.
ifneq ($(filter add_elf,$(MAKECMDGOALS)),)
ADD_ELF_POSITIONAL := $(filter-out add_elf,$(MAKECMDGOALS))
ifneq ($(strip $(ADD_ELF_POSITIONAL)),)
ifneq ($(words $(ADD_ELF_POSITIONAL)),1)
$(error usage: make add_elf /path/to/app.elf; do not combine with other targets)
endif
ifeq ($(filter %.elf,$(ADD_ELF_POSITIONAL)),)
$(error add_elf expects a path ending in .elf)
endif
ifneq ($(strip $(ELF)),)
$(error choose a positional path or ELF=, not both)
endif
endif
export ADD_ELF_REQUEST := $(if $(ELF),$(ELF),$(ADD_ELF_POSITIONAL))
ifneq ($(strip $(ADD_ELF_POSITIONAL)),)
.PHONY: $(ADD_ELF_POSITIONAL)
$(ADD_ELF_POSITIONAL): add_elf
	@:
endif
endif
.PHONY: add_elf test-add-elf
add_elf:
	python3 scripts/add_elf.py

test-add-elf: apps/hello.elf
	python3 tests/add_elf_test.py

# One explicit host block device; never expand /dev/* into formatting operations.
ifneq ($(filter format,$(MAKECMDGOALS)),)
FORMAT_POSITIONAL := $(filter-out format,$(MAKECMDGOALS))
ifneq ($(words $(FORMAT_POSITIONAL)),1)
$(error usage: sudo make format /dev/device; specify exactly one device)
endif
ifeq ($(filter /dev/%,$(FORMAT_POSITIONAL)),)
$(error format expects one /dev/device)
endif
export FORMAT_REQUEST := $(FORMAT_POSITIONAL)
.PHONY: $(FORMAT_POSITIONAL)
$(FORMAT_POSITIONAL): format
	@:
endif
.PHONY: format test-format-device
format:
	python3 scripts/format_device.py

test-format-device:
	python3 tests/format_device_test.py

.PHONY: test-execute
test-execute: barnix.iso
	python3 tests/execute_test.py

.PHONY: test-execute-setup
test-execute-setup: setup-build
	python3 tests/execute_setup_test.py

.PHONY: bcc bcc-demo
bcc: bcc-demo
bcc-demo: BDK/tools/bcc.py BDK/examples/bccdemo.c BDK/include/barnix_graphics.h BDK/runtime/graphics.c
	python3 BDK/tools/bcc.py BDK/examples/bccdemo.c -o BDK/bccdemo.elf

.PHONY: test-bcc-native
test-bcc-native: barnix.iso
	python3 tests/bcc_native_test.py

.PHONY: test-usb-hid test-usb-xhci
test-usb-hid:
	$(CC) -Wall -Wextra -I. tests/usb_hid_test.c usb_hid.c mouse.c -o /tmp/barnix-usb-hid-test
	/tmp/barnix-usb-hid-test

test-usb-xhci: barnix.iso
	USB_HOST=xhci python3 tests/usb_test.py

.PHONY: test-usb-ohci test-usb-ehci test-usb-hub test-usb-pointer test-usb-all
test-usb-ohci: barnix.iso
	USB_HOST=ohci python3 tests/usb_test.py

test-usb-ehci: barnix.iso
	USB_HOST=ehci python3 tests/usb_test.py

test-usb-hub: barnix.iso
	USB_HOST=xhci USB_HUB=1 python3 tests/usb_test.py

test-usb-pointer: barnix.iso
	python3 tests/usb_pointer_test.py
	USB_HOST=ohci python3 tests/usb_pointer_test.py
	USB_HOST=xhci python3 tests/usb_pointer_test.py
	USB_HOST=xhci USB_HUB=1 python3 tests/usb_pointer_test.py

test-usb-all: test-usb-hid test-mouse test-usb-companion test-usb test-usb-ohci test-usb-ehci test-usb-xhci test-usb-hub test-usb-pointer

.PHONY: test-usb-companion test-usb-boot
test-usb-companion: barnix.iso
	USB_HOST=ehci-companion python3 tests/usb_test.py
	USB_HOST=ehci-companion python3 tests/usb_pointer_test.py

test-usb-boot: all
	python3 Setup/test_usb.py
