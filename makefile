CC = gcc
LD = ld
OBJCOPY = objcopy

# Source directories: kernel core, filesystem, Linux ABI, shell and drivers.
KERNEL_DIRS = include/abi kernel kernel/fs kernel/linux kernel/shell drivers/input drivers/net drivers/sound drivers/storage drivers/usb drivers/video
INCLUDES = -I. $(addprefix -I,$(KERNEL_DIRS))

# WERROR=-Werror turns warnings into errors (CI uses it).
WERROR ?=
CFLAGS = -O2 -m32 -march=i386 -mno-sse -mno-mmx -msoft-float -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector -U_FORTIFY_SOURCE -Wall $(WERROR) $(INCLUDES)
LDFLAGS = -m elf_i386 -T kernel/linker.ld

OBJS = $(addprefix build/,$(addsuffix .o,\
	boot \
	kernel \
	barnix \
	fs \
	elf \
	elf_format \
	elf_enter \
	disk \
	storage_pci \
	wifi_iwl \
	net \
	net_e1000 \
	net_pcnet \
	net_realtek \
	net_tcp \
	net_vwifi \
	wifi \
	wifi_crypto \
	linux_task \
	linux_memory \
	linux_exec \
	linux_syscall \
	linux_idt \
	linux_int80 \
	linux_tss \
	linux_user \
	usb_storage \
	usb_hid \
	usb_xhci \
	usb_legacy \
	bssh \
	bssh_runner \
	shell_parse \
	keyboard \
	timer \
	mouse \
	text \
	font \
	console_video \
	video_rgb \
	memory \
	smp \
	smp_entry \
	audio \
	system_sound \
	settings \
	lang \
	))

DEPS = $(OBJS:.o=.d)
CFLAGS += -MMD -MP

.PHONY: all rebuild apps usb-disk run run-net run-net-pcnet run-cd run-cd-gcc run_usb clean distclean test test-elf test-shell test-usb test-repo test-web test-gouse-web test-wifi test-wifi-qemu test-text test-lang test-install run-setup
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

# Programs use SSE2 for float/double (as Windows 10 requires): Pentium 4 /
# Athlon 64 or newer. The kernel itself stays integer-only.
APP_FP = -march=pentium4 -mfpmath=sse
APP_CFLAGS = -m32 $(APP_FP) -O2 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -U_FORTIFY_SOURCE -Wall -Wextra $(INCLUDES) -Iapps -Ibarnixiolib
LINUX_COMMANDS = echo pwd cat ls touch write append cp true false
COMMANDS = beep play df diskinfo devices clear rm mkdir cd rmdir stat mv panic sync mount format unmount help init macro bnm get git brm chmod su sudo whoami useradd theme snake pong fdisk paint execute bcc barnocraft gouse gldemo glrunner java nova
APPS = apps/hello.elf apps/fileio.elf apps/theme.elf apps/snake.elf apps/asciiart.elf apps/excuse.elf apps/fortune.elf apps/pong.elf $(addprefix apps/bin/,$(sort $(COMMANDS) $(LINUX_COMMANDS)))

apps: $(APPS)

apps/%.elf: apps/%.c apps/start.c apps/runtime.c kernel/text.c barnixiolib/stdio.c barnixiolib/stdio.h apps/barnix_app.h include/abi/app_abi.h apps/app.ld scripts/mark_barnix_elf.py makefile
	$(CC) $(APP_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c barnixiolib/stdio.c $< -o $@
	python3 scripts/mark_barnix_elf.py $@

apps/bin/%: apps/%.c apps/command.h apps/start.c apps/runtime.c kernel/text.c barnixiolib/stdio.c barnixiolib/stdio.h apps/barnix_app.h include/abi/app_abi.h apps/app.ld scripts/mark_barnix_elf.py makefile
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c barnixiolib/stdio.c $< -o $@
	python3 scripts/mark_barnix_elf.py $@

# Execute shares the SDK graphics implementation, without a packaged SDK dependency.
apps/bin/execute: apps/execute.c apps/start.c apps/runtime.c kernel/text.c BDK/runtime/graphics.c BDK/include/barnix_graphics.h BDK/include/barnix_api.h apps/barnix_app.h include/abi/app_abi.h apps/app.ld makefile
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c BDK/runtime/graphics.c apps/execute.c -o $@
	python3 scripts/mark_barnix_elf.py $@

apps/bin/bcc: apps/bcc.c apps/bcc_template.inc apps/start.c apps/runtime.c kernel/text.c barnixiolib/stdio.c barnixiolib/stdio.h apps/barnix_app.h include/abi/app_abi.h apps/app.ld makefile
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c barnixiolib/stdio.c apps/bcc.c -o $@
	python3 scripts/mark_barnix_elf.py $@

# Barnocraft: mipmapped textures and the sine table are generated from the PNG;
# graphics and input come from the BDK Barnix Graphics library.
apps/barnocraft_assets.h: apps/barnocraft_textures.png scripts/barnocraft_assets.py
	python3 scripts/barnocraft_assets.py $< $@
# Sound effects: sounds/*.wav (CC0, see sounds/LICENSE.txt) become mu-law
# tables, one header per program; the name prefix picks the clips.
sound_args = $(foreach f,$(sort $(wildcard sounds/$(1)_*.wav)),$(patsubst $(1)_%,%,$(notdir $(basename $(f))))=$(f))
apps/barnocraft_sounds.h: $(wildcard sounds/bc_*.wav) scripts/sound_assets.py
	python3 scripts/sound_assets.py $@ bcs $(call sound_args,bc)
apps/gouse/gouse_sounds.h: $(wildcard sounds/g_*.wav) scripts/sound_assets.py
	python3 scripts/sound_assets.py $@ gs $(call sound_args,g)
apps/arcade_sounds.h: $(wildcard sounds/arc_*.wav) scripts/sound_assets.py
	python3 scripts/sound_assets.py $@ arc $(call sound_args,arc)
drivers/sound/system_sounds.h: $(wildcard sounds/sys_*.wav) scripts/sound_assets.py
	python3 scripts/sound_assets.py $@ sys $(call sound_args,sys)
BARNOCRAFT_CORE = apps/barnocraft_game.c apps/barnocraft_game.h apps/barnocraft_assets.h apps/barnocraft_sounds.h
# The raycaster runs per pixel: -O3 on top of the SSE2 program flags.
apps/barnocraft_game.o: $(BARNOCRAFT_CORE) BDK/include/barnix_gfx.h BDK/include/barnix_sound.h include/abi/app_abi.h include/abi/audio.h makefile
	$(CC) $(APP_CFLAGS) -O3 -DBARNIX_IN_TREE -IBDK/include -c apps/barnocraft_game.c -o $@
APP_GFX_DEPS = apps/command.h apps/start.c apps/runtime.c kernel/text.c BDK/runtime/gfx.c BDK/runtime/sound.c BDK/include/barnix_gfx.h BDK/include/barnix_sound.h include/abi/audio.h BDK/include/barnix_api.h apps/barnix_app.h include/abi/app_abi.h include/abi/keyboard.h include/abi/mouse.h apps/app.ld scripts/mark_barnix_elf.py makefile
# Sound commands use the BDK Barnix Sound library.
APP_SOUND_DEPS = apps/command.h apps/start.c apps/runtime.c kernel/text.c BDK/runtime/sound.c BDK/include/barnix_sound.h BDK/include/barnix_api.h apps/barnix_app.h include/abi/app_abi.h include/abi/audio.h apps/app.ld scripts/mark_barnix_elf.py makefile
apps/bin/beep apps/bin/play: apps/bin/%: apps/%.c $(APP_SOUND_DEPS)
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c BDK/runtime/sound.c $< -o $@
	python3 scripts/mark_barnix_elf.py $@

# Network commands use the BDK Barnix Net library (HTTP, TLS, inflate).
BNET_RUNTIME = BDK/runtime/net.c BDK/runtime/http.c BDK/runtime/tls.c BDK/runtime/x509.c BDK/runtime/pk.c BDK/runtime/crypto.c BDK/runtime/inflate.c
BNET_DEPS = $(BNET_RUNTIME) BDK/runtime/bnet.h BDK/runtime/tls.h BDK/include/barnix_net.h include/abi/netinfo.h apps/command.h apps/start.c apps/runtime.c kernel/text.c apps/barnix_app.h include/abi/app_abi.h apps/app.ld scripts/mark_barnix_elf.py makefile
apps/bin/get apps/bin/git apps/bin/brm: apps/bin/%: apps/%.c $(BNET_DEPS)
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c $(BNET_RUNTIME) $< -o $@
	python3 scripts/mark_barnix_elf.py $@

# Snake and Pong play arcade sounds.
apps/snake.elf apps/pong.elf: apps/%.elf: apps/%.c apps/game.h apps/arcade_sounds.h $(APP_SOUND_DEPS)
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c BDK/runtime/sound.c $< -o $@
	python3 scripts/mark_barnix_elf.py $@
apps/bin/snake apps/bin/pong: apps/bin/%: apps/%.c apps/game.h apps/arcade_sounds.h $(APP_SOUND_DEPS)
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c BDK/runtime/sound.c $< -o $@
	python3 scripts/mark_barnix_elf.py $@

# Barnix OpenGL: the BDK software OpenGL 1.1, float math in SSE2 registers.
BXGL_RUNTIME = BDK/runtime/gl.c BDK/runtime/gl_raster.c BDK/runtime/glu.c BDK/runtime/math.c
BXGL_DEPS = $(BXGL_RUNTIME) BDK/runtime/gl_internal.h BDK/include/GL/gl.h BDK/include/GL/glu.h BDK/include/barnix_opengl.h BDK/include/math.h
apps/bin/gldemo: apps/gldemo.c $(BXGL_DEPS) $(APP_GFX_DEPS)
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -IBDK/runtime -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--gc-sections -Wl,-T,apps/app.ld -ffunction-sections -fdata-sections apps/start.c apps/runtime.c kernel/text.c BDK/runtime/gfx.c $(BXGL_RUNTIME) $< -o $@
	python3 scripts/mark_barnix_elf.py $@

apps/bin/glrunner: apps/glrunner.c $(BXGL_DEPS) $(APP_GFX_DEPS)
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -IBDK/runtime -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--gc-sections -Wl,-T,apps/app.ld -ffunction-sections -fdata-sections apps/start.c apps/runtime.c kernel/text.c BDK/runtime/gfx.c BDK/runtime/sound.c $(BXGL_RUNTIME) $< -o $@
	python3 scripts/mark_barnix_elf.py $@

# BJVM: a Java 8 virtual machine with LWJGL 2 on Barnix OpenGL. The class
# library (apps/java/lib) is compiled by the host javac into rt.jar and
# linked into the program; tools/gen_gl.py writes the GL bindings.
JAVA_DIR = apps/java
JAVA_SOURCES = $(addprefix $(JAVA_DIR)/,vm.c heap.c class.c interp.c thread.c natives.c natives_core.c natives_io.c natives_lwjgl.c natives_gl.c main.c platform_barnix.c libc_barnix.c)
JAVA_LIB = $(shell find $(JAVA_DIR)/lib -name '*.java' 2>/dev/null)
$(JAVA_DIR)/obj/rt.jar: $(JAVA_LIB) $(JAVA_DIR)/build_rt.py
	mkdir -p $(JAVA_DIR)/obj
	python3 $(JAVA_DIR)/build_rt.py $@
$(JAVA_DIR)/natives_gl.c: $(JAVA_DIR)/tools/gen_gl.py $(JAVA_DIR)/tools/gl_ext.py BDK/include/GL/gl.h
	python3 $(JAVA_DIR)/tools/gen_gl.py
apps/bin/java: $(JAVA_SOURCES) $(JAVA_DIR)/jvm.h $(JAVA_DIR)/lwjgl_gl.h $(JAVA_DIR)/rt.S $(JAVA_DIR)/obj/rt.jar BDK/runtime/inflate.c $(BXGL_DEPS) $(APP_GFX_DEPS)
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -IBDK/runtime -I$(JAVA_DIR) -DRT_JAR='"$(JAVA_DIR)/obj/rt.jar"' -Wno-unused-parameter -Wno-override-init -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--gc-sections -Wl,-T,apps/app.ld -ffunction-sections -fdata-sections apps/start.c apps/runtime.c kernel/text.c BDK/runtime/gfx.c BDK/runtime/inflate.c $(BXGL_RUNTIME) $(JAVA_SOURCES) $(JAVA_DIR)/rt.S -lgcc -o $@
	python3 scripts/mark_barnix_elf.py $@

# Nova is a host-side compiler. Its Java 8 output runs on BJVM in Barnix.
NOVA_SOURCES = $(wildcard tools/nova/src/main/java/nova/*.java)
NOVA_EXAMPLES = $(patsubst tools/nova/examples/%.nova,tools/nova/build/examples/%.jar,$(wildcard tools/nova/examples/*.nova))
.PHONY: nova nova-check nova-image
nova: $(NOVA_EXAMPLES)
tools/nova/build/nova.jar: $(NOVA_SOURCES) tools/nova/build.sh
	bash tools/nova/build.sh
tools/nova/build/examples/%.jar: tools/nova/examples/%.nova tools/nova/build/nova.jar
	mkdir -p tools/nova/build/examples
	java -jar tools/nova/build/nova.jar build $< -o $@
nova-check: tools/nova/build/nova.jar
	bash tools/nova/tests/run.sh
nova-image: live-ext2.img nova
	python3 scripts/install_nova.py live-ext2.img tools/nova/build/examples

apps/bin/barnocraft: apps/barnocraft.c apps/barnocraft_game.o $(BARNOCRAFT_CORE) $(APP_GFX_DEPS)
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c BDK/runtime/gfx.c BDK/runtime/sound.c apps/barnocraft_game.o apps/barnocraft.c -o $@
	python3 scripts/mark_barnix_elf.py $@

# Happy Gouse: the windowed environment and its built-in applications.
# The browser engine and Gouse Web are built for size (-Os), so Happy Gouse
# stays compact alongside the JavaScript engine.
GOUSE_WEB_SOURCES = $(wildcard apps/gouse/web_*.c) apps/gouse/app_browser.c
GOUSE_WEB_OBJECTS = $(patsubst apps/gouse/%.c,apps/gouse/obj/%.o,$(GOUSE_WEB_SOURCES))
GOUSE_SOURCES = $(filter-out apps/gouse/js_platform.c $(GOUSE_WEB_SOURCES),$(wildcard apps/gouse/*.c))
# QuickJS is built with SSE2 math and a pinned, freestanding musl support archive.
WEB_LIBC = apps/gouse/obj/musl/lib/libc.a
WEB_JS_FLAGS = -m32 $(APP_FP) -Os -ffreestanding -fno-pie -fno-pic -fno-stack-protector -U_FORTIFY_SOURCE -ffunction-sections -fdata-sections -D_GNU_SOURCE -DBARNIX_JS -DCONFIG_VERSION='"2025-09-13"' -nostdinc -isystem apps/gouse/obj/musl/obj/include -isystem apps/gouse/obj/musl/include -isystem apps/gouse/obj/musl/arch/i386 -isystem apps/gouse/obj/musl/arch/generic -isystem $(shell $(CC) -print-file-name=include)
WEB_JS_NAMES = quickjs dtoa libregexp libunicode cutils
WEB_JS_HOST_OBJECTS = $(addprefix apps/gouse/obj/host_,$(addsuffix .o,$(WEB_JS_NAMES)))
apps/gouse/obj/host_%.o: apps/gouse/vendor/quickjs/%.c
	mkdir -p apps/gouse/obj
	$(CC) -O1 -g -fsanitize=address,undefined -D_GNU_SOURCE -DBARNIX_JS -DCONFIG_VERSION=\"2025-09-13\" -c $< -o $@
WEB_JS_OBJECTS = $(addprefix apps/gouse/obj/js_,$(addsuffix .o,$(WEB_JS_NAMES))) apps/gouse/obj/js_platform.o
$(WEB_LIBC): scripts/build_web_libc.py apps/gouse/vendor/musl-1.2.5.tar.gz
	python3 scripts/build_web_libc.py > /tmp/barnix-web-libc-build.log 2>&1
apps/gouse/obj/js_%.o: apps/gouse/vendor/quickjs/%.c $(WEB_LIBC)
	$(CC) $(WEB_JS_FLAGS) -DNDEBUG -c $< -o $@
apps/gouse/obj/js_platform.o: apps/gouse/js_platform.c $(WEB_LIBC)
	$(CC) $(WEB_JS_FLAGS) -Iapps/gouse/obj/musl/src/internal -Iapps/gouse/obj/musl/obj/src/internal -c $< -o $@
apps/gouse/obj/web_js.o apps/gouse/obj/web_canvas.o: apps/gouse/obj/%.o: apps/gouse/%.c apps/gouse/web.h apps/gouse/web_js.h $(WEB_LIBC)
	$(CC) $(WEB_JS_FLAGS) -Wall -Wextra -c $< -o $@
apps/gouse/obj/%.o: apps/gouse/%.c apps/gouse/web.h apps/gouse/gouse.h apps/gouse/gouse_assets.h $(APP_GFX_DEPS)
	mkdir -p apps/gouse/obj
	$(CC) $(filter-out -O2,$(APP_CFLAGS)) -Os -DBARNIX_IN_TREE -IBDK/include -c $< -o $@
apps/gouse/web.bwf: scripts/web_font.py
	python3 scripts/web_font.py $@

apps/gouse/gouse_assets.h: scripts/gouse_assets.py
	python3 scripts/gouse_assets.py $@
# Happy Gouse with Gouse Web: the browser engine (apps/gouse/web_*.c) and
# Barnix Net for HTTP/HTTPS. Its fonts are a data file (scripts/web_font.py).
apps/bin/gouse: $(WEB_JS_OBJECTS) $(WEB_LIBC) $(GOUSE_SOURCES) $(GOUSE_WEB_OBJECTS) apps/gouse/gouse.h apps/gouse/gouse_ru.h apps/gouse/gouse_assets.h apps/gouse/gouse_sounds.h apps/gouse/web.h apps/barnocraft_game.o $(BARNOCRAFT_CORE) $(APP_GFX_DEPS) $(BNET_DEPS)
	mkdir -p apps/bin
	$(CC) $(APP_CFLAGS) -DBARNIX_IN_TREE -IBDK/include -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,apps/app.ld apps/start.c apps/runtime.c kernel/text.c BDK/runtime/gfx.c BDK/runtime/sound.c $(BNET_RUNTIME) apps/barnocraft_game.o $(GOUSE_SOURCES) $(GOUSE_WEB_OBJECTS) $(WEB_JS_OBJECTS) $(WEB_LIBC) -lgcc -Wl,--gc-sections -o $@
	@size=$$(stat -c %s $@); if [ $$size -gt 16777216 ]; then echo "$@ is $$size bytes: more than the 16 MiB Barnix file limit"; exit 1; fi
	python3 scripts/mark_barnix_elf.py $@

apps/bin/%.elf: apps/%.elf makefile
	mkdir -p apps/bin
	cp $< $@

USER_CFLAGS = -m32 $(APP_FP) -O2 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -U_FORTIFY_SOURCE -Wall -Wextra

$(addprefix apps/bin/,$(LINUX_COMMANDS)): apps/bin/%: userland/commands.c userland/start.S userland/syscall.h include/uapi/unistd.h makefile
	mkdir -p apps/bin
	$(CC) $(USER_CFLAGS) -DCOMMAND='"$*"' -nostdlib -static -no-pie -Wl,--build-id=none userland/start.S userland/commands.c -o $@

USB_DISK ?= barnix-usb.img
NET_MODEL ?= e1000
QEMU_NET = -netdev user,id=net0 -device $(NET_MODEL),netdev=net0
# KVM when available, every core (up to 8), an SDL window that locks the
# mouse after a click (Ctrl+Alt+G releases it; QEMU's GTK window cannot hold
# the pointer on Wayland), and Intel HD Audio plus the PC speaker through
# PipeWire/PulseAudio. See scripts/qemu_host.py; QEMU_HOST= disables it all.
QEMU_HOST ?= $(shell python3 scripts/qemu_host.py qemu-system-i386)
# A Wi-Fi card and the WPA2 network "Quemu-Password-root" (password "root")
# for every run target: scripts/virtual_wifi.py starts the access point and
# QEMU with it. QEMU_WIFI= runs without Wi-Fi.
QEMU_WIFI ?= python3 scripts/virtual_wifi.py --

usb-disk: apps scripts/make_usb_disk.py
	python3 scripts/make_usb_disk.py --force $(USB_DISK) $(APPS)

test-elf: apps
	$(CC) -Wall -Wextra $(INCLUDES) tests/elf_test.c -o tests/elf_test
	./tests/elf_test $(APPS)

test:
	$(CC) -ffreestanding -fno-builtin -Wall -Wextra $(INCLUDES) tests/fs_test.c -o tests/fs_test
	python3 tests/ext2_test.py

test-shell: barnix.iso
	python3 tests/shell_test.py

test-usb: barnix.iso
	python3 tests/usb_test.py

test-wifi:
	$(CC) -O2 -Wall -Wextra $(INCLUDES) tests/wifi_crypto_test.c drivers/net/wifi_crypto.c -o tests/wifi_crypto_test
	python3 tests/wifi_crypto_test.py tests/wifi_crypto_test
	$(CC) -O2 -ffreestanding -fno-builtin -Wall -Wextra $(INCLUDES) -Dmemcpy=vw_memcpy -Dmemset=vw_memset -Dmemcmp=vw_memcmp -Dstrcpy=vw_strcpy -c drivers/net/net_vwifi.c -o tests/net_vwifi_host.o
	$(CC) -O2 -Wall -Wextra $(INCLUDES) tests/wifi_test.c drivers/net/wifi.c drivers/net/wifi_crypto.c tests/net_vwifi_host.o -o tests/wifi_test
	python3 tests/wifi_test.py tests/wifi_test

test-web: apps/gouse/web.bwf apps/gouse/web_bootstrap.h $(WEB_JS_HOST_OBJECTS)
	$(CC) -O1 -g -fsanitize=address,undefined -Wall -Wextra $(INCLUDES) -D_GNU_SOURCE -DBARNIX_JS -DCONFIG_VERSION=\"2025-09-13\" tests/web_render.c apps/gouse/web_*.c $(WEB_JS_HOST_OBJECTS) -lm -lz -o tests/web_render
	python3 tests/web_test.py tests/web_render apps/gouse/web.bwf
	$(CC) -O1 -g -fsanitize=address,undefined -Wall -Wextra $(INCLUDES) tests/web_jar_test.c apps/gouse/web_jar.c apps/gouse/web_util.c -o tests/web_jar_test
	./tests/web_jar_test

test-gouse-web: barnix.iso
	python3 tests/gouse_web_qemu_test.py

test-wifi-qemu: barnix.iso
	python3 tests/wifi_qemu_test.py

# One command for the whole suite. `check` runs host-only tests (seconds, no
# QEMU); `check-all` adds the QEMU boot tests (minutes, headless).
CHECK_HOST = test test-text test-shell-parse test-vfs test-font test-elf test-wifi test-permissions test-usb-hid test-mouse test-gl
CHECK_QEMU = test-shell test-lang test-linux test-pipe test-pipeline test-exec test-linux-commands test-linux-shell test-process-shell test-install test-execute test-add-elf
.PHONY: check check-all
check: $(CHECK_HOST)
	@echo "check: all host tests passed"
check-all: check $(CHECK_QEMU)
	@echo "check-all: all host and QEMU tests passed"

test-repo:
	bash -n scripts/update-repo.sh
	python3 tests/update_repo_test.py

# =========================
# Compile
# =========================

# Kernel objects go to build/; vpath finds each source in its directory.
vpath %.c $(KERNEL_DIRS)
vpath %.S $(KERNEL_DIRS)

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/system_sound.o: drivers/sound/system_sounds.h
build/bssh_runner.o: userland/bssh-run.elf
build/linux_probe.o: userland/linux-probe.elf

userland/bssh-run.elf: userland/bssh-run.c userland/start.S userland/syscall.h kernel/shell/shell_parse.c kernel/shell/shell_parse.h include/uapi/unistd.h makefile
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-Ttext-segment=0x0b000000 userland/start.S userland/bssh-run.c kernel/shell/shell_parse.c -o $@

# =========================
# Kernel
# =========================

kernel.elf: $(OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJS)

# =========================
# HDD image
# =========================

# The working disk: the live image grown to 1 GiB (sparse), room for GCC.
barnix-ext2.img: | live-ext2.img
	cp live-ext2.img $@
	truncate -s 1G $@ && resize2fs -p $@ >/dev/null && fallocate --dig-holes $@

live-ext2.img: $(APPS) $(NOVA_EXAMPLES) scripts/make_live_image.py scripts/install_apps.py scripts/install_nova.py scripts/install_minecraft.py scripts/install_firmware.py scripts/system_packages.py certs/roots.bin apps/gouse/web.bwf $(wildcard apps/gouse/examples/*) $(wildcard docs/manual/*)
	python3 scripts/make_live_image.py $@ $(APPS)

GCC_BUILD ?= /tmp/barnix-gcc-port/build2/gcc
BINUTILS_BUILD ?= /tmp/barnix-binutils-port/build

live-ext2-gcc.img: $(APPS) scripts/make_live_image.py scripts/install_apps.py scripts/install_gcc_toolchain.py certs/roots.bin apps/gouse/web.bwf $(wildcard apps/gouse/examples/*)
	python3 scripts/make_live_image.py --gcc-build $(GCC_BUILD) --binutils-build $(BINUTILS_BUILD) $@ $(APPS)

# =========================
# ISO
# =========================

# Hybrid: Legacy BIOS (i386-pc) and UEFI (x86_64-efi), also on a USB stick.
barnix.iso: kernel.elf kernel-efi.elf live-ext2.img kernel/grub.cfg
	mkdir -p $(GRUB_DIR)
	cp kernel.elf kernel-efi.elf $(BOOT_DIR)/
	cp live-ext2.img $(BOOT_DIR)/
	cp kernel/grub.cfg $(GRUB_DIR)/
	grub-mkrescue -o barnix.iso $(ISO_DIR) -- -volid BARNIX

GCC_ISO_DIR = $(ISO_DIR)-gcc
barnix-gcc.iso: kernel.elf kernel-efi.elf live-ext2-gcc.img kernel/grub.cfg
	mkdir -p $(GCC_ISO_DIR)/boot/grub
	cp kernel.elf kernel-efi.elf $(GCC_ISO_DIR)/boot/
	cp live-ext2-gcc.img $(GCC_ISO_DIR)/boot/live-ext2.img
	cp kernel/grub.cfg $(GCC_ISO_DIR)/boot/grub/
	grub-mkrescue -o $@ $(GCC_ISO_DIR) -- -volid BARNIX

# =========================
# Run
# =========================

ifeq ($(FIRMWARE),efi)
run: run-installed
else
# 2 GiB: GCC (brm -S gcc) needs about 1.5 GiB to run from Happy Gouse.
run: barnix.iso barnix-ext2.img
	$(QEMU_WIFI) python3 scripts/managed_disks.py run $(QEMU_HOST) \
		-m 2048M \
		-boot d \
		-drive file=barnix.iso,media=cdrom,if=ide,index=3 \
		-drive file=barnix-ext2.img,format=raw,if=ide \
		$(QEMU_NET)

endif

run-net: barnix.iso barnix-ext2.img
	$(QEMU_WIFI) python3 scripts/managed_disks.py run $(QEMU_HOST) -m 2048M -boot d -drive file=barnix.iso,media=cdrom,if=ide,index=3 \
		-drive file=barnix-ext2.img,format=raw,if=ide \
		$(QEMU_NET)

run-net-pcnet: barnix.iso barnix-ext2.img
	$(QEMU_WIFI) python3 scripts/managed_disks.py run $(QEMU_HOST) -m 512M -boot d -drive file=barnix.iso,media=cdrom,if=ide,index=3 \
		-drive file=barnix-ext2.img,format=raw,if=ide \
		-netdev user,id=net0 -device pcnet,netdev=net0

run-cd: barnix.iso
	$(QEMU_WIFI) python3 scripts/managed_disks.py run $(QEMU_HOST) -m 512M -boot d -drive file=barnix.iso,media=cdrom,if=ide,index=3 $(QEMU_NET)

run-cd-gcc: barnix-gcc.iso
	$(QEMU_WIFI) python3 scripts/managed_disks.py run $(QEMU_HOST) -m 2048M -boot d -drive file=barnix-gcc.iso,media=cdrom,if=ide,index=3 $(QEMU_NET)

run_usb: barnix.iso apps
	@if test ! -f "$(USB_DISK)"; then \
		python3 scripts/make_usb_disk.py "$(USB_DISK)" $(APPS); \
	fi
	$(QEMU_WIFI) python3 scripts/managed_disks.py run $(QEMU_HOST) \
		-m 512M -boot d \
		-drive file=barnix.iso,media=cdrom,if=ide,index=3 \
		$(QEMU_NET) \
		-device piix3-usb-uhci,id=uhci \
		-drive if=none,id=flashfile,file=$(USB_DISK),format=raw \
		-device usb-storage,bus=uhci.0,drive=flashfile,id=flash

# =========================
# Clean
# =========================

clean:
	rm -rf build
	rm -f tests/fs_test tests/vfs_test tests/text_test tests/shell_parse_test
	rm -rf apps/gouse/obj
	rm -f tests/elf_test $(APPS) apps/barnocraft_assets.h apps/gouse/gouse_assets.h apps/barnocraft_game.o apps/barnocraft_sounds.h apps/gouse/gouse_sounds.h apps/arcade_sounds.h drivers/sound/system_sounds.h
	rm -f kernel.elf kernel-test.elf userland/linux-probe.elf userland/exec-parent.elf userland/exec-child.elf userland/fork-probe.elf
	rm -f userland/bssh-run.elf userland/pipe-probe.elf
	rm -f barnix.iso barnix-legacy-setup.iso barnix-efi-setup.iso kernel-efi.elf
	rm -f Setup/kernel.elf Setup/kernel-efi.elf
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
	$(CC) -m32 -fno-builtin -Wall -Wextra $(INCLUDES) tests/text_test.c kernel/text.c drivers/input/keyboard.c kernel/timer.c drivers/input/mouse.c kernel/lang.c -o tests/text_test
	./tests/text_test

test-lang: barnix.iso
	python3 tests/lang_test.py

userland/linux-probe.elf: userland/linux-probe.S userland/linux-file-probe.c
	$(CC) $(APP_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none userland/linux-probe.S userland/linux-file-probe.c -o $@


.PHONY: test-linux
test-linux: kernel-test.elf live-ext2.img
	python3 tests/linux_test.py

.PHONY: test-linux-large-loader
test-linux-large-loader: kernel-test.elf live-ext2.img userland/linux-probe.elf
	python3 tests/linux_large_loader_test.py

userland/linux-high-probe.elf: userland/linux-probe.S userland/linux-file-probe.c
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-Ttext-segment=0x80000000 $^ -o $@

.PHONY: test-linux-large-window
test-linux-large-window: kernel-test.elf live-ext2.img userland/linux-high-probe.elf
	python3 tests/linux_large_window_test.py

.PHONY: test-linux-big-file
test-linux-big-file: kernel-test.elf userland/linux-probe.elf
	python3 tests/linux_big_file_test.py

.PHONY: test-linux-shell
test-linux-shell: barnix.iso userland/linux-probe.elf
	python3 tests/linux_shell_test.py

.PHONY: test-linux-commands
test-linux-commands: apps
	python3 tests/linux_commands_test.py

# The normal kernel contains neither the ABI probe nor its test boot entry.
build/kernel-test.o: kernel/kernel.c makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBARNIX_LINUX_SELFTEST -c $< -o $@

build/linux_selftest.o: tests/linux_selftest.c kernel/linux/linux_exec.h kernel/fs/fs.h kernel/barnix.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

kernel-test.elf: $(filter-out build/kernel.o,$(OBJS)) build/kernel-test.o build/linux_selftest.o build/linux_probe.o kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(filter-out build/kernel.o,$(OBJS)) build/kernel-test.o build/linux_selftest.o build/linux_probe.o

-include build/kernel-test.d build/linux_selftest.d

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
# Barnix OpenGL on the host: off-screen rendering, pixel checks, PPM images in tests/gl.
test-gl:
	mkdir -p tests/gl
	for f in gl gl_raster glu math; do $(CC) -std=c11 -O2 -Wall -Wextra -Werror -DBXGL_HOST -ffreestanding -fno-builtin -nostdinc -isystem $$($(CC) -print-file-name=include) -IBDK/include -IBDK/runtime -c BDK/runtime/$$f.c -o tests/gl/$$f.o || exit 1; done
	$(CC) -std=gnu11 -O2 -fno-builtin -Wall -Wextra -Werror -idirafter BDK/include tests/gl_test.c tests/gl/gl.o tests/gl/gl_raster.o tests/gl/glu.o tests/gl/math.o -o tests/gl/gl_test
	./tests/gl/gl_test tests/gl

test-shell-parse:
	$(CC) -Wall -Wextra -Werror $(INCLUDES) tests/shell_parse_test.c kernel/shell/shell_parse.c -o tests/shell_parse_test
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
	$(CC) -ffreestanding -fno-builtin -Wall -Wextra $(INCLUDES) tests/vfs_test.c -o tests/vfs_test
	python3 tests/vfs_test.py

.PHONY: test-disks
test-disks: barnix.iso
	python3 tests/disks_test.py

.PHONY: test-img
test-img:
	python3 tests/managed_disks_test.py

.PHONY: test-font
test-font:
	$(CC) -Wall -Wextra $(INCLUDES) tests/font_test.c kernel/text.c -o /tmp/barnix-font-test
	/tmp/barnix-font-test

# Separate LiveCD installer; the regular kernel does not contain the wizard.
build/setup/kernel.o: kernel/kernel.c Setup/setup.h makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBARNIX_SETUP -c $< -o $@
Setup/build/profiles.h: Setup/profiles.json Setup/build.py
	python3 Setup/build.py --header
build/setup/wizard.o: Setup/wizard.c Setup/setup.h Setup/build/profiles.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
build/setup/package.o: Setup/package.c Setup/package.h Setup/sha256.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
build/setup/sha256.o: Setup/sha256.c Setup/sha256.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
SETUP_EXTRA = build/setup/package.o build/setup/sha256.o
Setup/kernel.elf: $(filter-out build/kernel.o,$(OBJS)) build/setup/kernel.o build/setup/wizard.o $(SETUP_EXTRA) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(filter-out build/kernel.o,$(OBJS)) build/setup/kernel.o build/setup/wizard.o $(SETUP_EXTRA)
.PHONY: setup setup-build setup-build-gcc run-setup run-setup-gcc test-setup
FIRMWARE ?= legacy
export SETUP_DISK
setup: setup-build
SETUP_ISOS = barnix-legacy-setup.iso barnix-efi-setup.iso
setup-build: $(SETUP_ISOS)
$(SETUP_ISOS) &: kernel.elf kernel-efi.elf live-ext2.img $(NOVA_EXAMPLES) Setup/kernel.elf Setup/kernel-efi.elf Setup/build.py Setup/packages.py Setup/profiles.json scripts/install_apps.py scripts/install_nova.py scripts/add_elf.py $(wildcard Setup/extra-apps Setup/extra-apps/*.elf)
	python3 Setup/packages.py --gcc-image live-ext2-gcc.img
	python3 Setup/build.py
run-setup: setup-build
	python3 Setup/run.py --firmware $(FIRMWARE)
GCC_SETUP_ISOS = barnix-legacy-setup-gcc.iso barnix-efi-setup-gcc.iso
setup-build-gcc: $(GCC_SETUP_ISOS)
$(GCC_SETUP_ISOS) &: $(SETUP_ISOS) live-ext2-gcc.img Setup/build.py Setup/packages.py scripts/install_gcc_toolchain.py Setup/build/packages/sha256sums Setup/build/packages/network.bpk Setup/build/packages/games.bpk Setup/build/packages/gcc.bpk
	python3 Setup/packages.py --gcc-build $(GCC_BUILD) --binutils-build $(BINUTILS_BUILD)
	python3 Setup/build.py --gcc-iso
run-setup-gcc: setup-build-gcc
	python3 Setup/run.py --gcc --firmware $(FIRMWARE)
.PHONY: test-setup-gcc
test-setup-gcc: setup-build-gcc
	python3 tests/setup_gcc_install_test.py
	python3 tests/setup_gcc_efi_boot_test.py
.PHONY: run-installed test-setup-launcher
run-installed:
	python3 Setup/run.py --installed --firmware $(FIRMWARE)
test-setup-launcher:
	python3 tests/setup_launcher_test.py
.PHONY: test-setup-boot
test-setup-boot: setup-build
	python3 tests/setup_boot_test.py
test-setup: setup-build
	python3 Setup/test_setup.py
-include build/setup/kernel.d build/setup/wizard.d build/setup/package.d build/setup/sha256.d

.PHONY: test-large-fs
test-large-fs:
	$(CC) -O2 -ffreestanding -fno-builtin -Wall -Wextra $(INCLUDES) tests/large_fs_test.c -o tests/large_fs_test
	python3 tests/large_fs_test.py

.PHONY: test-storage-pci
test-storage-pci: barnix.iso
	python3 tests/storage_pci_test.py

.PHONY: test-setup-storage
test-setup-storage: setup-build
	python3 Setup/test_storage.py

# UEFI keeps the 32-bit kernel ABI; GRUB supplies a GOP framebuffer.
build/boot-efi.o: kernel/boot.S makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBARNIX_EFI -c $< -o $@
build/kernel-efi.o: kernel/kernel.c makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBARNIX_EFI -c $< -o $@
kernel-efi.elf: $(filter-out build/kernel.o build/boot.o,$(OBJS)) build/boot-efi.o build/kernel-efi.o kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(filter-out build/kernel.o build/boot.o,$(OBJS)) build/boot-efi.o build/kernel-efi.o
build/setup/kernel-efi.o: kernel/kernel.c Setup/setup.h makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBARNIX_SETUP -DBARNIX_EFI -c $< -o $@
build/setup/wizard-efi.o: Setup/wizard.c Setup/setup.h Setup/build/profiles.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBARNIX_EFI -c $< -o $@
Setup/kernel-efi.elf: $(filter-out build/kernel.o build/boot.o,$(OBJS)) build/boot-efi.o build/setup/kernel-efi.o build/setup/wizard-efi.o $(SETUP_EXTRA) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(filter-out build/kernel.o build/boot.o,$(OBJS)) build/boot-efi.o build/setup/kernel-efi.o build/setup/wizard-efi.o $(SETUP_EXTRA)
-include build/boot-efi.d build/kernel-efi.d build/setup/kernel-efi.d build/setup/wizard-efi.d

.PHONY: test-setup-efi
test-setup-efi: setup-build
	python3 Setup/test_efi.py

.PHONY: test-permissions
test-permissions:
	$(CC) -O2 -ffreestanding -fno-builtin -Wall -Wextra $(INCLUDES) tests/permissions_test.c -o tests/permissions_test
	python3 tests/permissions_test.py

.PHONY: test-system-permissions
test-system-permissions: all
	python3 tests/system_permissions.py

.PHONY: test-partitions
test-partitions: barnix.iso
	python3 tests/partition_shell_test.py

.PHONY: test-mouse
test-mouse:
	$(CC) -Wall -Wextra $(INCLUDES) tests/mouse_test.c drivers/input/mouse.c -o /tmp/barnix-mouse-test
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
	$(CC) -Wall -Wextra $(INCLUDES) tests/usb_hid_test.c drivers/usb/usb_hid.c drivers/input/mouse.c -o /tmp/barnix-usb-hid-test
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

.PHONY: add-drive
DRIVE_IMG ?= drives/usb_drive.img
DRIVE_MOUNT ?= /mnt/barnix_drive

add-drive:
	@mkdir -p drives
	@if [ ! -f "$(DRIVE_IMG)" ]; then \
		python3 scripts/make_usb_disk.py --force $(DRIVE_IMG); \
	fi
	@echo "Drive image created at $(DRIVE_IMG)"
	@mkdir -p $(DRIVE_MOUNT)
	@if ! mountpoint -q $(DRIVE_MOUNT); then \
		sudo mount -o loop $(DRIVE_IMG) $(DRIVE_MOUNT) || echo "Warning: Could not mount $(DRIVE_IMG). Try running with sudo."; \
	fi
	@echo "Drive mounted at $(DRIVE_MOUNT)"

apps/gouse/web_bootstrap.h: apps/gouse/web_bootstrap.js scripts/web_bootstrap.py
	python3 scripts/web_bootstrap.py
apps/gouse/obj/web_js.o: apps/gouse/web_bootstrap.h

.PHONY: test-web-js
test-web-js: apps/gouse/web_bootstrap.h $(WEB_JS_HOST_OBJECTS)
	$(CC) -O1 -g -fsanitize=address,undefined $(INCLUDES) -D_GNU_SOURCE -DBARNIX_JS -DCONFIG_VERSION=\"2025-09-13\" tests/web_js_test.c apps/gouse/web_*.c $(WEB_JS_HOST_OBJECTS) -lm -lz -o tests/web_js_test
	./tests/web_js_test

.PHONY: test-bulk-fs
test-bulk-fs:
	$(CC) -O2 -ffreestanding -fno-builtin -Wall -Wextra $(INCLUDES) tests/bulk_fs_test.c -o tests/bulk_fs_test
	rm -f tests/bulk_fs_test.img && truncate -s 128M tests/bulk_fs_test.img
	./tests/bulk_fs_test tests/bulk_fs_test.img
	e2fsck -fn tests/bulk_fs_test.img
	rm -f tests/bulk_fs_test.img

# =========================
# x86_64 port (branch x86_64), stage 0: long-mode boot on BIOS and UEFI
# =========================
CFLAGS64 = -O2 -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector -U_FORTIFY_SOURCE -fno-asynchronous-unwind-tables -Wall -Wextra $(WERROR) $(INCLUDES) -MMD -MP
LDFLAGS64 = -m elf_x86_64 -z max-page-size=0x1000 -T kernel/arch/x86_64/linker.ld
OBJS64 = build/x86_64/boot.o build/x86_64/stage0.o

build/x86_64/%.o: kernel/arch/x86_64/%.S makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS64) -c $< -o $@
build/x86_64/%.o: kernel/arch/x86_64/%.c makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS64) -c $< -o $@
kernel64.elf: $(OBJS64) kernel/arch/x86_64/linker.ld
	$(LD) $(LDFLAGS64) -o $@ $(OBJS64)
-include $(OBJS64:.o=.d)

barnix64.iso: kernel64.elf
	mkdir -p build/iso64/boot/grub
	cp kernel64.elf build/iso64/boot/
	printf 'set timeout=0\nmenuentry "Barnix x86_64" {\n    multiboot /boot/kernel64.elf\n    boot\n}\n' > build/iso64/boot/grub/grub.cfg
	grub-mkrescue -o $@ build/iso64 -- -volid BARNIX64 2>/dev/null

.PHONY: test-x86_64 run64
test-x86_64: barnix64.iso
	python3 tests/x86_64_boot_test.py
run64: barnix64.iso
	qemu-system-x86_64 -m 2048M -cdrom barnix64.iso
