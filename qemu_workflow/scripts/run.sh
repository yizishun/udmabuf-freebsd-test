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
    -drive file="$DISK_IMG",if=virtio,format=qcow2 \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-pci,netdev=net0 \
    -display egl-headless \
    -vga none \
    -object memory-backend-ram,id=mem1,size=2048M,share=on \
    -machine memory-backend=mem1 \
    -device virtio-gpu-pci,blob=true,hostmem=2048M \
    -vnc :0 \
    -trace "udmabuf*" \
    -serial stdio
    #-trace "virtio_gpu_cmd_*" \
