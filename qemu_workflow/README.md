# qemu workflow for test udmabuf in freebsd
qemu is a actually users of udmabuf, 
and the final goal is making qemu with udmabuf feature run properly in freebsd.


However, before that, we should patch qemu to make it compile and run in freebsd 
with udmabuf feature, and use specific config to enable it.
this is the purpose of this foler

# Usage
```sh
# apply patch to qemu
gmake patch QEMU_SRC=/path/to/qemu

# build and run
gmake build QEMU_SRC=/path/to/qemu DRM_KMOD=/path/to/drm-kmod
gmake run QEMU_SRC=/path/to/qemu DRM_KMOD=/path/to/drm-kmod
```

# More
The critical line in qemu run.sh is `-device virtio-gpu-pci,blob=true,hostmem=2048M`
which enable virtio-gpu-pci (Don't use virtio-gpu-gl-pci, which will not use udmabuf)


# TODO
now, you should see something like this
```
gmake run QEMU_SRC=../../qemu DRM_KMOD=../../drm-kmod
+ QEMU_SRC_DIR=../../qemu
+ [ -z ../../qemu ]
+ ISO_IMG=test_guest_alpine-virt.iso
+ DISK_IMG=test_disk.qcow2
+ echo '=== Running QEMU with Udmabuf ==='
=== Running QEMU with Udmabuf ===
+ ../../qemu/build/qemu-system-x86_64 -m 2048 -smp 2 -drive 'file=test_disk.qcow2,if=virtio,format=qcow2' -netdev 'user,id=net0,hostfwd=tcp::2222-:22' -device 'virtio-net-pci,netdev=net0' -display egl-headless -vga none -object 'memory-backend-ram,id=mem1,size=2048M,share=on' -machine 'memory-backend=mem1' -device 'virtio-gpu-pci,blob=true,hostmem=2048M' -vnc :0 -trace 'udmabuf*' -serial stdio
No such file or directory
qemu-system-x86_64: -device virtio-gpu-pci,blob=true,hostmem=2048M: warning: open /dev/udmabuf: No such file or directory
qemu-system-x86_64: -device virtio-gpu-pci,blob=true,hostmem=2048M: need rutabaga or udmabuf for blob resources
gmake: *** [Makefile:19: run] Error 1
```