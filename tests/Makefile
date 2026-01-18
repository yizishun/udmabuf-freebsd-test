TESTS=		udmabuf_test_simple udmabuf_test_linux

LDFLAGS+=	-L/usr/local/lib -latf-c
CFLAGS+=	-I/usr/local/include
CFLAGS+= 	-I${DRMKMOD_DIR}/linuxkpi/gplv2/include/uapi
CFLAGS+= 	-I${DRMKMOD_DIR}/linuxkpi/gplv2/include
CFLAGS+= 	-I${DRMKMOD_DIR}/linuxkpi/bsd/include
CFLAGS+= 	-I${SYSDIR}/compat/linuxkpi/common/include
CFLAGS+= 	-I${SYSDIR}/compat/linuxkpi/dummy/include

all: $(TESTS)

udmabuf_test_simple: udmabuf_test_simple.c
	$(CC) $(CFLAGS) -o $@ udmabuf_test_simple.c $(LDFLAGS)

udmabuf_test_linux: udmabuf_test_linux.c
	$(CC) $(CFLAGS) -o $@ udmabuf_test_linux.c $(LDFLAGS)

clean:
	rm -f $(TESTS) *.o
