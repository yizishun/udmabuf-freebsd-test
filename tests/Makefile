TESTS=		udmabuf_test_ioctl_fam

# Use base-system private ATF library so the binary runs in nfsroot
# without requiring ports.  The headers live under /usr/include/atf-c
# and the shared lib is libprivateatf-c.so.1.
LDFLAGS+=	-lprivateatf-c
CFLAGS+=	-I. -I/home/yzs/freebsd/freebsd-src/sys

all: $(TESTS)

udmabuf_test_linux_bsd: udmabuf_test_linux_bsd.c
	$(CC) $(CFLAGS) -o $@ udmabuf_test_linux_bsd.c $(LDFLAGS)

udmabuf_test_ioctl_fam: udmabuf_test_ioctl_fam.c
	$(CC) $(CFLAGS) -o $@ udmabuf_test_ioctl_fam.c $(LDFLAGS)

clean:
	rm -f $(TESTS) *.o
