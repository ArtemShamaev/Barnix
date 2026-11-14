#!/bin/sh
# Host test build of BJVM (32-bit, glibc), with the BDK software OpenGL and
# no window (host_gfx.c). Usage: sh build_host.sh [extra gcc flags]
set -e
cd "$(dirname "$0")"
B=../../BDK
for f in inflate gl gl_raster glu; do
    gcc -m32 -O2 -g -c -DBXGL_HOST -I$B/include -I$B/runtime $B/runtime/$f.c -o obj/host_$f.o
done
# platform_host.c uses glibc's stdio, which the BDK headers replace.
gcc -m32 -O1 -g -Wall -c -I. platform_host.c -o obj/host_platform.o
gcc -m32 -O1 -g -Wall -c -I. -I$B/include host_gfx.c -o obj/host_gfx.o
gcc -m32 -O1 -g -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -Wno-override-init "$@" \
    -DBJVM_HOST -I. -I$B/include -DRT_JAR='"obj/rt.jar"' \
    vm.c heap.c class.c interp.c thread.c natives.c natives_core.c natives_io.c natives_lwjgl.c natives_gl.c main.c \
    obj/host_platform.o obj/host_gfx.o obj/host_inflate.o obj/host_gl.o obj/host_gl_raster.o obj/host_glu.o rt.S -lm -o obj/java-host
