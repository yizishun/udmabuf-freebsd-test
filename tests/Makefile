TESTS=		udmabuf_test_simple udmabuf_test_linux_bsd

LDFLAGS+=	-L/usr/local/lib -latf-c
CFLAGS+=	-I/usr/local/include
CFLAGS+= 	-I.

all: $(TESTS)

udmabuf_test_simple: udmabuf_test_simple.c
	$(CC) $(CFLAGS) -o $@ udmabuf_test_simple.c $(LDFLAGS)

udmabuf_test_linux_bsd: udmabuf_test_linux_bsd.c
	$(CC) $(CFLAGS) -o $@ udmabuf_test_linux_bsd.c $(LDFLAGS)

clean:
	rm -f $(TESTS) *.o
