# qemu workflow for test udmabuf in freebsd
qemu is a actually users of udmabuf, 
and the final goal is making qemu with udmabuf feature run properly in freebsd.


However, before that, we should patch qemu to make it compile and run in freebsd 
with udmabuf feature, and use specific config to enable it.
this is the purpose of this foler

# Usage
```sh
# apply patch to qemu
make patch QEMU_SRC=/path/to/qemu

# build and run
make build_and_run QEMU_SRC=/path/to/qemu DRM_KMOD=/path/to/drm-kmod
```