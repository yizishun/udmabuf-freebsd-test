#!/bin/sh
set -xe # Prints the huge QEMU command before running it

QEMU_SRC_DIR=$1

if [ -z "$QEMU_SRC_DIR" ]; then
    echo "Usage: $0 <path-to-qemu-src>"
    exit 1
fi

ISO_IMG="test_guest_alpine-virt.iso"
DISK_IMG="test_disk.qcow2"

echo "=== Running QEMU with Udmabuf ==="

"$QEMU_SRC_DIR/build/qemu-system-x86_64" \
    -m 2048 \
    -smp 2 \
    -cdrom "$ISO_IMG" \
    -drive file="$DISK_IMG",if=virtio,format=qcow2 \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-pci,netdev=net0 \
    -display egl-headless \
    -vnc :0 \
    -device virtio-gpu-pci \
    -vga none \
    -serial stdio