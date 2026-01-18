/*-
 * Copyright (c) 2026 Zishun Yi.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>

#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <atf-c/tc.h>
#include <atf-c.h>

#include <udmabuf.h>

#define NUM_PAGES       4 /* in pages */
#define NUM_SCATTERS    4 /* number of scatter entries */

static struct udmabuf_create
init_create(uint32_t memfd, off64_t offset, off64_t size, uint32_t flags)
{
	struct udmabuf_create create;

	create.memfd  = memfd;
	create.offset = offset;
	create.size   = size;
	create.flags  = flags;

	return create;
}

static int init_memfd(off64_t size, bool hpage)
{
	int memfd;
	unsigned int flags = MFD_ALLOW_SEALING;

	if (hpage)
		flags |= MFD_HUGETLB;

	ATF_REQUIRE((memfd = memfd_create("udmabuf-test", flags)) >= 0);
	ATF_REQUIRE(fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK) >= 0);
	ATF_REQUIRE(ftruncate(memfd, size) != -1);

	return memfd;
}

static int create_udmabuf_scatter_list(int devfd, int *memfds, int count, off_t size_per_fd)
{
	struct udmabuf_create_list *list;
	int ubuf_fd, i;

	ATF_REQUIRE((list = malloc(sizeof(struct udmabuf_create_list) +
				sizeof(struct udmabuf_create_item) * count)) != NULL);

	list->count = count;
	list->flags = UDMABUF_FLAGS_CLOEXEC;

	for (i = 0; i < count; i++) {
		list->list[i].memfd  = memfds[i];
		list->list[i].offset = 0;
		list->list[i].size   = size_per_fd;
	}

	ATF_REQUIRE((ubuf_fd = ioctl(devfd, UDMABUF_CREATE_LIST, list)) >= 0);
	free(list);
	return ubuf_fd;
}

static void *mmap_fd(int fd, off64_t size)
{
	void *addr;

	ATF_REQUIRE((addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) != MAP_FAILED);

	return addr;
}

static void compare_scatter_data(void *base_addr, int num_scatters, size_t chunk_size)
{
	char *base = (char *)base_addr;
	int i;
	size_t j;

	for (i = 0; i < num_scatters; i++) {
		char *current_chunk = base + (i * chunk_size);
		
		char expected_val = (char)(i % 255);

		for (j = 0; j < chunk_size; j++) {
			if (current_chunk[j] != expected_val) {
				atf_tc_fail("Data mismatch at scatter %d, offset %zu. Expected %d, got %d",
				i, j, expected_val, current_chunk[j]);
			}
		}
	}
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
	int devfd, memfd;
	unsigned int page_size = getpagesize();
	unsigned int mem_size = page_size * NUM_PAGES;

	ATF_REQUIRE((devfd = open("/dev/udmabuf", O_RDWR)) >= 0);
	memfd = init_memfd(mem_size, false);
	create = init_create(memfd, 1, mem_size, UDMABUF_FLAGS_CLOEXEC);
	ATF_REQUIRE(ioctl(devfd, UDMABUF_CREATE, &create) < 0);

	close(memfd);
	close(devfd);
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
	int devfd, memfd;
	unsigned int page_size = getpagesize();
	unsigned int mem_size = page_size * NUM_PAGES;

	ATF_REQUIRE((devfd = open("/dev/udmabuf", O_RDWR)) >= 0);
	memfd = init_memfd(mem_size, false);
	create = init_create(memfd, 0, mem_size + 1, UDMABUF_FLAGS_CLOEXEC);
	ATF_REQUIRE(ioctl(devfd, UDMABUF_CREATE, &create) < 0);

	close(memfd);
	close(devfd);
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
	int devfd, memfd;
	unsigned int page_size = getpagesize();
	unsigned int mem_size = page_size * NUM_PAGES;

	ATF_REQUIRE((devfd = open("/dev/udmabuf", O_RDWR)) >= 0);
	create = init_create(0, 0, mem_size, UDMABUF_FLAGS_CLOEXEC);
	ATF_REQUIRE(ioctl(devfd, UDMABUF_CREATE, &create) < 0);

	close(memfd);
	close(devfd);
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
	int devfd, memfd, ubuf_fd;
	unsigned int page_size = getpagesize();
	unsigned int mem_size = page_size * NUM_PAGES;

	ATF_REQUIRE((devfd = open("/dev/udmabuf", O_RDWR)) >= 0);
	memfd = init_memfd(mem_size, false);
	create = init_create(memfd, 0, mem_size, UDMABUF_FLAGS_CLOEXEC);
	ATF_REQUIRE((ubuf_fd = ioctl(devfd, UDMABUF_CREATE, &create)) >= 0);

	close(ubuf_fd);
	close(memfd);
	close(devfd);
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
	int devfd, ubuf_fd;
	int memfds[NUM_SCATTERS];
	void *ubuf_map_addr, *memfd_map_addr;
	int i;
	unsigned int page_size = getpagesize();
	unsigned int size_per_fd = page_size * NUM_PAGES;
	off_t total_size = size_per_fd * NUM_SCATTERS;

	ATF_REQUIRE((devfd = open("/dev/udmabuf", O_RDWR)) >= 0);

	for (i = 0; i < NUM_SCATTERS; i++) {
		memfds[i] = init_memfd(size_per_fd, false);
		memfd_map_addr = mmap_fd(memfds[i], size_per_fd);
		memset(memfd_map_addr, (i % 255), size_per_fd);
		munmap(memfd_map_addr, size_per_fd);
	}

	ubuf_fd = create_udmabuf_scatter_list(devfd, memfds, NUM_SCATTERS, size_per_fd);
	ubuf_map_addr = mmap_fd(ubuf_fd, total_size);

	compare_scatter_data(ubuf_map_addr, NUM_SCATTERS, size_per_fd);

	munmap(ubuf_map_addr, total_size);
	close(ubuf_fd);
	for (i = 0; i < NUM_SCATTERS; i++) close(memfds[i]);
	close(devfd);
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
	int devfd, ubuf_fd;
	int memfds[NUM_SCATTERS];
	void *ubuf_map_addr, *memfd_map_addr;
	int i;
	unsigned int page_size = getpagesize() * 512; /* 2 MB */
	unsigned int size_per_fd = page_size * NUM_PAGES;
	off_t total_size = size_per_fd * NUM_SCATTERS;

	ATF_REQUIRE((devfd = open("/dev/udmabuf", O_RDWR)) >= 0);

	for (i = 0; i < NUM_SCATTERS; i++) {
		memfds[i] = init_memfd(size_per_fd, true);
		memfd_map_addr = mmap_fd(memfds[i], size_per_fd);
		memset(memfd_map_addr, (i % 255), size_per_fd);
		munmap(memfd_map_addr, size_per_fd);
	}

	ubuf_fd = create_udmabuf_scatter_list(devfd, memfds, NUM_SCATTERS, size_per_fd);
	ubuf_map_addr = mmap_fd(ubuf_fd, total_size);

	compare_scatter_data(ubuf_map_addr, NUM_SCATTERS, size_per_fd);

	munmap(ubuf_map_addr, total_size);
	close(ubuf_fd);
	for (i = 0; i < NUM_SCATTERS; i++) close(memfds[i]);
	close(devfd);
}

/*
 * Test Case 7: Huge Table Pin Check
 */
ATF_TC(huge_table_pin_check);
ATF_TC_HEAD(huge_table_pin_check, tc)
{
	atf_tc_set_md_var(tc, "descr", "migration of 2MB size huge pages, pin first, write after");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(huge_table_pin_check, tc)
{
	int devfd, ubuf_fd;
	int memfds[NUM_SCATTERS];
	void *ubuf_map_addr, *memfd_map_addr;
	int i;
	unsigned int page_size = getpagesize() * 512; /* 2 MB */
	unsigned int size_per_fd = page_size * NUM_PAGES;
	off_t total_size = size_per_fd * NUM_SCATTERS;

	ATF_REQUIRE((devfd = open("/dev/udmabuf", O_RDWR)) >= 0);

	for (i = 0; i < NUM_SCATTERS; i++) {
		memfds[i] = init_memfd(size_per_fd, true);
	}

	ubuf_fd = create_udmabuf_scatter_list(devfd, memfds, NUM_SCATTERS, size_per_fd);
	ubuf_map_addr = mmap_fd(ubuf_fd, total_size);

	for (i = 0; i < NUM_SCATTERS; i++) {
		memfd_map_addr = mmap_fd(memfds[i], size_per_fd);
		memset(memfd_map_addr, (i % 255), size_per_fd);
		munmap(memfd_map_addr, size_per_fd);
	}

	compare_scatter_data(ubuf_map_addr, NUM_SCATTERS, size_per_fd);

	munmap(ubuf_map_addr, total_size);
	close(ubuf_fd);
	for (i = 0; i < NUM_SCATTERS; i++) close(memfds[i]);
	close(devfd);
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