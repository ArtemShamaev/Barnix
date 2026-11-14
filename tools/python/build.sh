#!/bin/sh
# Builds Python 3.9 for Barnix: a static i386 Linux program (Barnix runs
# those through its Linux layer) with the modules of Setup.local built in,
# and a trimmed standard library compiled to .pyc.
#
#   sh tools/python/build.sh          ->  tools/python/root/usr/{bin,lib}
#
# Needs gcc with -m32 and the 32-bit glibc (lib32-glibc), as the GCC port.
# scripts/system_packages.py packs tools/python/root as the brm package
# "python"; the IDE runs .py files with it.
set -eu
VERSION=3.9.25
SHA256=00e07d7c0f2f0cc002432d1ee84d2a40dae404a99303e3f97701c10966c91834
here="$(cd "$(dirname "$0")" && pwd)"
work="$here/build"
root="$here/root"
mkdir -p "$work"
cd "$work"
tarball="Python-$VERSION.tar.xz"
if [ ! -f "$tarball" ]; then
    curl -fL -o "$tarball.part" "https://www.python.org/ftp/python/$VERSION/$tarball"
    mv "$tarball.part" "$tarball"
fi
echo "$SHA256  $tarball" | sha256sum -c -
rm -rf "Python-$VERSION"
tar xf "$tarball"
cd "Python-$VERSION"
cp "$here/Setup.local" Modules/Setup.local
# Not a cross build: the host runs the i386 result (configure tests, pybuilddir).
# Linked at 0x80000000: Barnix runs big Linux programs (GCC, Python) in its large window.
CC="gcc -m32" CFLAGS="-std=gnu11 -O2" LDFLAGS="-static -m32 -Wl,-Ttext-segment=0x80000000" \
    ./configure --prefix=/usr --disable-shared --without-ensurepip > ../configure.log 2>&1 \
    || { tail -20 ../configure.log; exit 1; }
make -j"$(nproc)" python pybuilddir.txt > ../make.log 2>&1 || { tail -30 ../make.log; exit 1; }
file python | grep -q "statically linked" || { echo "python is not static"; exit 1; }

rm -rf "$root"
mkdir -p "$root/usr/bin" "$root/usr/lib/python3.9"
cp python "$root/usr/bin/python3"
strip "$root/usr/bin/python3"
# The standard library without tests, GUI and packaging tools Barnix cannot use.
tar -C Lib -cf - --exclude=test --exclude=tests --exclude=idlelib --exclude=tkinter \
    --exclude=turtledemo --exclude=ensurepip --exclude=lib2to3 --exclude=distutils \
    --exclude=venv --exclude='__pycache__' . | tar -C "$root/usr/lib/python3.9" -xf -
cp build/lib.*/_sysconfigdata_*.py "$root/usr/lib/python3.9/" 2>/dev/null || true
# Python finds its platform part (exec_prefix) by this folder; Barnix packages carry files.
mkdir -p "$root/usr/lib/python3.9/lib-dynload" && : > "$root/usr/lib/python3.9/lib-dynload/.keep"
# Compiled once here, so Barnix starts programs quickly (users cannot write /usr).
./python -E -m compileall -q -j0 -d /usr/lib/python3.9 "$root/usr/lib/python3.9" || true
echo "Python $VERSION for Barnix: $root ($(du -sh "$root" | cut -f1))"
