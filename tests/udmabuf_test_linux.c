// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __EXPORTED_HEADERS__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdbool.h>

#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <atf-c/tc.h>
#include <atf-c.h>

typedef uint8_t u8;
typedef uint8_t __u8;
typedef uint16_t u16;
typedef uint16_t __u16;
typedef uint32_t u32;
typedef uint32_t __u32;
typedef uint64_t u64;
typedef uint64_t __u64;

typedef int8_t s8;
typedef int8_t __s8;
typedef int16_t s16;
typedef int16_t __s16;
typedef int32_t s32;
typedef int32_t __s32;
typedef int64_t s64;
typedef int64_t __s64;
#include <linux/udmabuf.h>

#define TEST_PREFIX	"drivers/dma-buf/udmabuf"
#define NUM_PAGES       4
#define NUM_ENTRIES     4
#define MEMFD_SIZE      1024 /* in pages */

static int create_memfd_with_seals(off64_t size, bool hpage)
{
	int memfd, ret;
	unsigned int flags = MFD_ALLOW_SEALING;

	if (hpage)
		flags |= MFD_HUGETLB;

	memfd = memfd_create("udmabuf-test", flags);
	if (memfd < 0) {
		atf_tc_skip("%s: [skip,no-memfd]\n", TEST_PREFIX);
	}

	ret = fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret < 0) {
		atf_tc_skip("%s: [skip,fcntl-add-seals]\n", TEST_PREFIX);
	}

	ret = ftruncate(memfd, size);
	if (ret == -1) {
		atf_tc_fail("%s: [FAIL,memfd-truncate]\n", TEST_PREFIX);
	}

	return memfd;
}

static int create_udmabuf_list(int devfd, int memfd, off64_t memfd_size)
{
	struct udmabuf_create_list *list;
	int ubuf_fd, i;

	list = malloc(sizeof(struct udmabuf_create_list) +
		      sizeof(struct udmabuf_create_item) * NUM_ENTRIES);
	if (!list) {
		atf_tc_fail("%s: [FAIL, udmabuf-malloc]\n", TEST_PREFIX);
	}

	for (i = 0; i < NUM_ENTRIES; i++) {
		list->list[i].memfd  = memfd;
		list->list[i].offset = i * (memfd_size / NUM_ENTRIES);
		list->list[i].size   = getpagesize() * NUM_PAGES;
	}

	list->count = NUM_ENTRIES;
	list->flags = UDMABUF_FLAGS_CLOEXEC;
	ubuf_fd = ioctl(devfd, UDMABUF_CREATE_LIST, list);
	free(list);
	if (ubuf_fd < 0) {
		atf_tc_fail("%s: [FAIL, udmabuf-create]\n", TEST_PREFIX);
	}

	return ubuf_fd;
}

static void write_to_memfd(void *addr, off64_t size, char chr, unsigned int page_size)
{
	int i;

	for (i = 0; i < size / page_size; i++) {
		*((char *)addr + (i * page_size)) = chr;
	}
}

static void *mmap_fd(int fd, off64_t size)
{
	void *addr;

	addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		atf_tc_fail("%s: ubuf_fd mmap fail\n", TEST_PREFIX);
	}

	return addr;
}

static int compare_chunks(void *addr1, void *addr2, off64_t memfd_size)
{
	off64_t off;
	int i = 0, j, k = 0, ret = 0;
	char char1, char2;

	while (i < NUM_ENTRIES) {
		off = i * (memfd_size / NUM_ENTRIES);
		for (j = 0; j < NUM_PAGES; j++, k++) {
			char1 = *((char *)addr1 + off + (j * getpagesize()));
			char2 = *((char *)addr2 + (k * getpagesize()));
			if (char1 != char2) {
				ret = -1;
				goto err;
			}
		}
		i++;
	}
err:
	munmap(addr1, memfd_size);
	munmap(addr2, NUM_ENTRIES * NUM_PAGES * getpagesize());
	return ret;
}

/*
 * Test Case 1: Offset Alignment Check
 */
ATF_TC(offset_align_check);
ATF_TC_HEAD(offset_align_check, tc)
{
	atf_tc_set_md_var(tc, "descr", "offset not page aligned, should fail");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(offset_align_check, tc)
{
	struct udmabuf_create create;
	int devfd, memfd, buf;
	off64_t size;

	// common part
	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		atf_tc_fail(
			"%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
			TEST_PREFIX);
	}
	size = getpagesize() * NUM_PAGES;
	memset(&create, 0, sizeof(create));

	memfd = create_memfd_with_seals(size, false);

	/* should fail (offset not page aligned) */
	create.memfd  = memfd;
	create.offset = getpagesize()/2;
	create.size   = getpagesize();
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0)
		atf_tc_fail("%s: [FAIL,test-1]\n", TEST_PREFIX);
	else
		atf_tc_pass();
}

/*
 * Test Case 2: Size Alignment Check
 */
ATF_TC(size_align_check);
ATF_TC_HEAD(size_align_check, tc)
{
	atf_tc_set_md_var(tc, "descr", "size not page aligned, should fail");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(size_align_check, tc)
{
	struct udmabuf_create create;
	int devfd, memfd, buf;
	off64_t size;

	// common part
	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		atf_tc_fail(
			"%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
			TEST_PREFIX);
	}
	size = getpagesize() * NUM_PAGES;
	memset(&create, 0, sizeof(create));

	memfd = create_memfd_with_seals(size, false);

	/* should fail (size not multiple of page) */
	create.memfd  = memfd;
	create.offset = 0;
	create.size   = getpagesize()/2;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0)
		atf_tc_fail("%s: [FAIL,test-2]\n", TEST_PREFIX);
	else
		atf_tc_pass();
}

/*
 * Test Case 3: Mem fd Check
 */
ATF_TC(memfd_check);
ATF_TC_HEAD(memfd_check, tc)
{
	atf_tc_set_md_var(tc, "descr", "use valid memfd");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(memfd_check, tc)
{
	struct udmabuf_create create;
	int devfd, memfd, buf;
	off64_t size;

	// common part
	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		atf_tc_fail(
			"%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
			TEST_PREFIX);
	}
	size = getpagesize() * NUM_PAGES;
	memset(&create, 0, sizeof(create));

	/* should fail (not memfd) */
	create.memfd  = 0; /* stdin */
	create.offset = 0;
	create.size   = size;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0)
		atf_tc_fail("%s: [FAIL,test-3]\n", TEST_PREFIX);
	else
		atf_tc_pass();
}

/*
 * Test Case 4: Normal case Check
 */
ATF_TC(normal_case_check);
ATF_TC_HEAD(normal_case_check, tc)
{
	atf_tc_set_md_var(tc, "descr", "normal case, should work");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(normal_case_check, tc)
{
	struct udmabuf_create create;
	int devfd, memfd, buf;
	off64_t size;
	void *addr1;
	unsigned int page_size;

	// common part
	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		atf_tc_fail(
			"%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
			TEST_PREFIX);
	}
	size = getpagesize() * NUM_PAGES;
	memset(&create, 0, sizeof(create));

	memfd = create_memfd_with_seals(size, false);

	/* should work */
	page_size = getpagesize();
	addr1 = mmap_fd(memfd, size);
	write_to_memfd(addr1, size, 'a', page_size);
	create.memfd  = memfd;
	create.offset = 0;
	create.size   = size;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf < 0)
		atf_tc_fail("%s: [FAIL,test-4]\n", TEST_PREFIX);
	else {
		atf_tc_pass();
	}
}

/*
 * Test Case 5: Create list Check
 */
ATF_TC(create_list_check);
ATF_TC_HEAD(create_list_check, tc)
{
	atf_tc_set_md_var(tc, "descr", "migration of 4K size huge pages");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(create_list_check, tc)
{
	struct udmabuf_create create;
	int devfd, memfd, buf, ret;
	off64_t size;
	void *addr1, *addr2;
	unsigned int page_size;

	// common part
	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		atf_tc_fail(
			"%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
			TEST_PREFIX);
	}
	memset(&create, 0, sizeof(create));

	/* should work (migration of 4k size pages)*/
	page_size = getpagesize();
	size = MEMFD_SIZE * page_size;
	memfd = create_memfd_with_seals(size, false);
	addr1 = mmap_fd(memfd, size);
	write_to_memfd(addr1, size, 'a', page_size);
	buf = create_udmabuf_list(devfd, memfd, size);
	addr2 = mmap_fd(buf, NUM_PAGES * NUM_ENTRIES * getpagesize());
	write_to_memfd(addr1, size, 'b', page_size);
	ret = compare_chunks(addr1, addr2, size);
	if (ret < 0)
		atf_tc_fail("%s: [FAIL,test-5]\n", TEST_PREFIX);
	else {
		atf_tc_pass();
	}

}

/*
 * Test Case 6: Huge Table Check
 */
ATF_TC(huge_table_check);
ATF_TC_HEAD(huge_table_check, tc)
{
	atf_tc_set_md_var(tc, "descr", "migration of 2MB size huge pages");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(huge_table_check, tc)
{
	struct udmabuf_create create;
	int devfd, memfd, buf, ret;
	off64_t size;
	void *addr1, *addr2;
	unsigned int page_size;

	// common part
	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		atf_tc_fail(
			"%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
			TEST_PREFIX);
	}
	memset(&create, 0, sizeof(create));

	/* should work (migration of 2MB size huge pages)*/
	page_size = getpagesize() * 512; /* 2 MB */
	size = MEMFD_SIZE * page_size;
	memfd = create_memfd_with_seals(size, true);
	addr1 = mmap_fd(memfd, size);
	write_to_memfd(addr1, size, 'a', page_size);
	buf = create_udmabuf_list(devfd, memfd, size);
	addr2 = mmap_fd(buf, NUM_PAGES * NUM_ENTRIES * getpagesize());
	write_to_memfd(addr1, size, 'b', page_size);
	ret = compare_chunks(addr1, addr2, size);
	if (ret < 0)
		atf_tc_fail("%s: [FAIL,test-6]\n", TEST_PREFIX);
	else {
		atf_tc_pass();
	}

}

/*
 * Test Case 7: Huge Table Pin Check
 */
ATF_TC(huge_table_pin_check);
ATF_TC_HEAD(huge_table_pin_check, tc)
{
	atf_tc_set_md_var(tc, "descr", "migration of 2MB size huge pages, pin first");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(huge_table_pin_check, tc)
{
	struct udmabuf_create create;
	int devfd, memfd, buf, ret;
	off64_t size;
	void *addr1, *addr2;
	unsigned int page_size;

	// common part
	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		atf_tc_fail(
			"%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
			TEST_PREFIX);
	}
	memset(&create, 0, sizeof(create));

	/* same test as above but we pin first before writing to memfd */
	page_size = getpagesize() * 512; /* 2 MB */
	size = MEMFD_SIZE * page_size;
	memfd = create_memfd_with_seals(size, true);
	buf = create_udmabuf_list(devfd, memfd, size);
	addr2 = mmap_fd(buf, NUM_PAGES * NUM_ENTRIES * getpagesize());
	addr1 = mmap_fd(memfd, size);
	write_to_memfd(addr1, size, 'a', page_size);
	write_to_memfd(addr1, size, 'b', page_size);
	ret = compare_chunks(addr1, addr2, size);
	if (ret < 0)
		atf_tc_fail("%s: [FAIL,test-7]\n", TEST_PREFIX);
	else {
		atf_tc_pass();
	}

}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, offset_align_check);
	ATF_TP_ADD_TC(tp, size_align_check);
	ATF_TP_ADD_TC(tp, normal_case_check);
	ATF_TP_ADD_TC(tp, memfd_check);
	ATF_TP_ADD_TC(tp, create_list_check);
	ATF_TP_ADD_TC(tp, huge_table_check);
	ATF_TP_ADD_TC(tp, huge_table_pin_check);

	return (atf_no_error());
}