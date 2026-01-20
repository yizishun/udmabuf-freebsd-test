#!/bin/sh
set -xe # x = print commands, e = exit on error

QEMU_SRC_DIR=$1
DRM_KMOD_DIR=$2

if [ -z "$QEMU_SRC_DIR" ] || [ -z "$DRM_KMOD_DIR" ]; then
    echo "Usage: $0 <path-to-qemu-src> <path-to-drm-kmod>"
    exit 1
fi

echo "=== Building QEMU in $QEMU_SRC_DIR ==="

mkdir -p "$QEMU_SRC_DIR/build"
cd "$QEMU_SRC_DIR/build"

# Only run configure if Makefile doesn't exist to save time, 
# or force it if you changed flags.
# For now, we run it every time to be safe as you requested.
../configure --target-list=x86_64-softmmu \
             --enable-debug \
             --enable-opengl \
             --enable-gtk \
             --extra-cflags="-I/usr/local/include" \
             --extra-cflags="-I$DRM_KMOD_DIR/linuxkpi/gplv2/include/uapi" \
             --extra-ldflags="-L/usr/local/lib -linotify"

ninja -j$(sysctl -n hw.ncpu)