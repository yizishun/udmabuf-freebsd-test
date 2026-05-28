/*-
 * Copyright (c) 2026 Zishun Yi.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Test the Generic Netlink API for udmabuf.
 *
 * The FreeBSD kernel udmabuf driver now uses Generic Netlink instead
 * of ioctl() and flexible array members (FAM). These tests exercise
 * the UDMABUF_CMD_CREATE_LIST netlink command, validating the parsing
 * of nested attributes (UDMABUF_ATTR_LISTS and UDMABUF_ATTR_ITEM).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <atf-c/tc.h>
#include <atf-c.h>

#include <udmabuf.h>

/* Netlink and SNL includes */
#include <netlink/netlink.h>
#include <netlink/netlink_generic.h>
#include <netlink/netlink_snl.h>
#include <netlink/netlink_snl_generic.h>

/* snl udmabuf socket helper */
static inline bool
snl_init_udmabuf(struct snl_state *ss, int netlink_family)
{
	if(snl_init(ss, netlink_family) == false) {
		return (false);
	}
	int val = 1;
	socklen_t optlen = sizeof(val);
	if(setsockopt(ss->fd, SOL_NETLINK, NETLINK_SND_SYNC, &val, optlen) == -1) {
		snl_free(ss);
		return (false);
	}
	return (true);
}

/* ------------------------------------------------------------------ */
/* Netlink Reply Parser Setup                                         */
/* ------------------------------------------------------------------ */

struct nl_parsed_reply {
	uint32_t dmabuf_fd;
};

static const struct snl_field_parser nlf_p_empty[] = {};

#define _OUT(_field) offsetof(struct nl_parsed_reply, _field)
static const struct snl_attr_parser ap_reply[] = {
	{ .type = UDMABUF_ATTR_DMABUF, .off = _OUT(dmabuf_fd), .cb = snl_attr_get_uint32 },
};
#undef _OUT
SNL_DECLARE_PARSER(reply_parser, struct genlmsghdr, nlf_p_empty, ap_reply);

/* ------------------------------------------------------------------ */
/* Netlink Message Builder Helpers                                    */
/* ------------------------------------------------------------------ */

static void *
add_attr(struct nlmsghdr *nlh, uint16_t type, uint16_t len, const void *data)
{
	struct nlattr *nla = (struct nlattr *)((char *)nlh + NLA_ALIGN(nlh->nlmsg_len));
	nla->nla_type = type;
	nla->nla_len = NLA_HDRLEN + len;
	
	if (data != NULL && len > 0)
		memcpy((char *)nla + NLA_HDRLEN, data, len);
		
	nlh->nlmsg_len = NLA_ALIGN(nlh->nlmsg_len) + NLA_ALIGN(nla->nla_len);
	return (nla);
}

static void
add_attr_u32(struct nlmsghdr *nlh, uint16_t type, uint32_t data)
{
	add_attr(nlh, type, sizeof(data), &data);
}

static void
add_attr_u64(struct nlmsghdr *nlh, uint16_t type, uint64_t data)
{
	add_attr(nlh, type, sizeof(data), &data);
}

static struct nlattr *
add_nested_start(struct nlmsghdr *nlh, uint16_t type)
{
	struct nlattr *nla = (struct nlattr *)((char *)nlh + NLA_ALIGN(nlh->nlmsg_len));
	nla->nla_type = type;
	/* Temporarily reserve header space, will be finalized in end() */
	nlh->nlmsg_len = NLA_ALIGN(nlh->nlmsg_len) + NLA_HDRLEN;
	return (nla);
}

static void
add_nested_end(struct nlmsghdr *nlh, struct nlattr *nest)
{
	nest->nla_len = (char *)nlh + nlh->nlmsg_len - (char *)nest;
	nlh->nlmsg_len = NLA_ALIGN(nlh->nlmsg_len);
}

/* ------------------------------------------------------------------ */
/* Test Helpers                                                       */
/* ------------------------------------------------------------------ */

struct udmabuf_uapi_item {
	int memfd;
	uint64_t offset;
	uint64_t size;
};

static int
init_memfd(off_t size, bool hpage)
{
	int memfd;
	unsigned int flags = MFD_ALLOW_SEALING;

	if (hpage)
		flags |= MFD_HUGETLB;

	ATF_REQUIRE((memfd = memfd_create("udmabuf-nl-test", flags)) >= 0);
	ATF_REQUIRE(fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK) >= 0);
	ATF_REQUIRE(ftruncate(memfd, size) != -1);

	printf("fd = %d\n", memfd);
	fflush(stdout);
	return (memfd);
}

/* Helper to wait for and parse the Netlink ACK/Reply */
static int
nl_get_reply_fd(struct snl_state *ss, uint32_t seq, uint16_t family_id)
{
	struct nlmsghdr *hdr;
	int out_fd = -1;
	int out_err = 0;

	while ((hdr = snl_read_message(ss)) != NULL) {
		if (hdr->nlmsg_seq != seq)
			continue;

		if (hdr->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(hdr);
			if (err->error != 0) {
				out_err = err->error;
			}
		} else if (hdr->nlmsg_type == family_id) {
			struct nl_parsed_reply reply = { .dmabuf_fd = -1 };
			if (snl_parse_nlmsg(ss, hdr, &reply_parser, &reply)) {
				out_fd = reply.dmabuf_fd;
			}
		}

		if (hdr->nlmsg_type == NLMSG_DONE || hdr->nlmsg_type == NLMSG_ERROR)
			break;
	}

	if (out_err != 0) {
		/* Netlink errors are negative, convert to standard errno */
		errno = -out_err;
		return (-1);
	}
	return (out_fd);
}

static int
create_udmabuf_list(struct snl_state *ss, uint16_t family_id,
    struct udmabuf_uapi_item *items, int count, uint32_t flags)
{
	/* Allocate enough buffer for genl header + many nested TLVs */
	size_t buf_size = 1024 + count * 128;
	struct nlmsghdr *nlh = calloc(1, buf_size);
	int fd;
	ATF_REQUIRE(nlh != NULL);

	nlh->nlmsg_type = family_id;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->nlmsg_seq = snl_get_seq(ss);
	nlh->nlmsg_len = sizeof(struct nlmsghdr) + sizeof(struct genlmsghdr);

	struct genlmsghdr *ghdr = (struct genlmsghdr *)(nlh + 1);
	ghdr->cmd = UDMABUF_CMD_CREATE_LIST;
	ghdr->version = 1;

	add_attr_u32(nlh, UDMABUF_ATTR_FLAGS, flags);

	/* Start outer list container */
	struct nlattr *list_nla = add_nested_start(nlh, UDMABUF_ATTR_LISTS);
	for (int i = 0; i < count; i++) {
		/* Add inner item container */
		struct nlattr *item_nla = add_nested_start(nlh, UDMABUF_ATTR_ITEM);
		add_attr_u32(nlh, UDMABUF_ATTR_MEMFD, items[i].memfd);
		add_attr_u64(nlh, UDMABUF_ATTR_OFFSET, items[i].offset);
		add_attr_u64(nlh, UDMABUF_ATTR_SIZE, items[i].size);
		add_nested_end(nlh, item_nla);
	}
	add_nested_end(nlh, list_nla);

	uint32_t seq = nlh->nlmsg_seq;
	bool sent = snl_send(ss, nlh, nlh->nlmsg_len);
	free(nlh);
	
	ATF_REQUIRE(sent);

	fd = nl_get_reply_fd(ss, seq, family_id);
	printf("user get dmabuf fd: %d\n", fd);
	return fd;
}

/* ------------------------------------------------------------------ */
/* Test: nl_single_item                                               */
/* ------------------------------------------------------------------ */

ATF_TC(nl_single_item);
ATF_TC_HEAD(nl_single_item, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "UDMABUF_CREATE_LIST with count=1 via Netlink");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_single_item, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	int ubuf_fd;
	size_t mem_size = getpagesize() * 4;
	struct udmabuf_uapi_item item;

	ATF_REQUIRE(snl_init_udmabuf(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	item.memfd = init_memfd(mem_size, false);
	item.offset = 0;
	item.size = mem_size;

	ubuf_fd = create_udmabuf_list(&ss, family_id, &item, 1, UDMABUF_FLAGS_CLOEXEC);
	ATF_REQUIRE(ubuf_fd >= 0);

	close(ubuf_fd);
	close(item.memfd);
	snl_free(&ss);
}

/* ------------------------------------------------------------------ */
/* Test: nl_multiple_items                                            */
/* ------------------------------------------------------------------ */

ATF_TC(nl_multiple_items);
ATF_TC_HEAD(nl_multiple_items, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "UDMABUF_CREATE_LIST with multiple items via Netlink TLV nesting");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_multiple_items, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	int ubuf_fd, i;
	size_t size_per_fd = getpagesize() * 4;
	struct udmabuf_uapi_item items[8];

	ATF_REQUIRE(snl_init_udmabuf(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	for (i = 0; i < 8; i++) {
		items[i].memfd = init_memfd(size_per_fd, false);
		items[i].offset = 0;
		items[i].size = size_per_fd;
	}

	ubuf_fd = create_udmabuf_list(&ss, family_id, items, 8, UDMABUF_FLAGS_CLOEXEC);
	ATF_REQUIRE(ubuf_fd >= 0);

	close(ubuf_fd);
	for (i = 0; i < 8; i++)
		close(items[i].memfd);
	snl_free(&ss);
}

/* ------------------------------------------------------------------ */
/* Test: nl_offset_in_item                                            */
/* ------------------------------------------------------------------ */

ATF_TC(nl_offset_in_item);
ATF_TC_HEAD(nl_offset_in_item, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Non-zero offsets in Netlink nested items are correctly mapped");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_offset_in_item, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	int ubuf_fd;
	size_t page_size = getpagesize();
	struct udmabuf_uapi_item item;

	ATF_REQUIRE(snl_init_udmabuf(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	item.memfd = init_memfd(page_size * 8, false);
	item.offset = page_size * 2;
	item.size = page_size * 4;

	ubuf_fd = create_udmabuf_list(&ss, family_id, &item, 1, UDMABUF_FLAGS_CLOEXEC);
	ATF_REQUIRE(ubuf_fd >= 0);

	close(ubuf_fd);
	close(item.memfd);
	snl_free(&ss);
}

/* ------------------------------------------------------------------ */
/* Test: nl_without_cloexec                                           */
/* ------------------------------------------------------------------ */

ATF_TC(nl_without_cloexec);
ATF_TC_HEAD(nl_without_cloexec, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "UDMABUF_CREATE_LIST without CLOEXEC via Netlink");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_without_cloexec, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	int ubuf_fd;
	size_t mem_size = getpagesize() * 4;
	struct udmabuf_uapi_item item;

	ATF_REQUIRE(snl_init_udmabuf(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	item.memfd = init_memfd(mem_size, false);
	item.offset = 0;
	item.size = mem_size;

	ubuf_fd = create_udmabuf_list(&ss, family_id, &item, 1, 0);
	ATF_REQUIRE(ubuf_fd >= 0);

	/* Verify the fd is NOT close-on-exec */
	ATF_REQUIRE((fcntl(ubuf_fd, F_GETFD) & FD_CLOEXEC) == 0);

	close(ubuf_fd);
	close(item.memfd);
	snl_free(&ss);
}

/* ------------------------------------------------------------------ */
/* Test: nl_with_cloexec                                              */
/* ------------------------------------------------------------------ */

ATF_TC(nl_with_cloexec);
ATF_TC_HEAD(nl_with_cloexec, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "UDMABUF_CREATE_LIST with CLOEXEC via Netlink");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_with_cloexec, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	int ubuf_fd;
	size_t mem_size = getpagesize() * 4;
	struct udmabuf_uapi_item item;

	ATF_REQUIRE(snl_init_udmabuf(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	item.memfd = init_memfd(mem_size, false);
	item.offset = 0;
	item.size = mem_size;

	ubuf_fd = create_udmabuf_list(&ss, family_id, &item, 1, UDMABUF_FLAGS_CLOEXEC);
	ATF_REQUIRE(ubuf_fd >= 0);

	/* Verify the fd IS close-on-exec */
	ATF_REQUIRE((fcntl(ubuf_fd, F_GETFD) & FD_CLOEXEC) != 0);

	close(ubuf_fd);
	close(item.memfd);
	snl_free(&ss);
}

/* ------------------------------------------------------------------ */
/* Test: nl_invalid_memfd                                             */
/* ------------------------------------------------------------------ */

ATF_TC(nl_invalid_memfd);
ATF_TC_HEAD(nl_invalid_memfd, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Invalid memfd in Netlink item should cause failure");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_invalid_memfd, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	struct udmabuf_uapi_item item;

	ATF_REQUIRE(snl_init_udmabuf(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	item.memfd = -1;
	item.offset = 0;
	item.size = getpagesize() * 4;

	ATF_REQUIRE(create_udmabuf_list(&ss, family_id, &item, 1, UDMABUF_FLAGS_CLOEXEC) < 0);
	snl_free(&ss);
}

/* ------------------------------------------------------------------ */
/* Test: nl_misaligned_offset_in_item                                 */
/* ------------------------------------------------------------------ */

ATF_TC(nl_misaligned_offset_in_item);
ATF_TC_HEAD(nl_misaligned_offset_in_item, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Misaligned offset in Netlink item should fail");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_misaligned_offset_in_item, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	struct udmabuf_uapi_item item;

	ATF_REQUIRE(snl_init_udmabuf(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	item.memfd = init_memfd(getpagesize() * 8, false);
	item.offset = 1; /* Not page aligned */
	item.size = getpagesize() * 4;

	ATF_REQUIRE(create_udmabuf_list(&ss, family_id, &item, 1, UDMABUF_FLAGS_CLOEXEC) < 0);

	close(item.memfd);
	snl_free(&ss);
}

/* ------------------------------------------------------------------ */
/* Test: nl_count_limit                                               */
/* ------------------------------------------------------------------ */

ATF_TC(nl_count_limit);
ATF_TC_HEAD(nl_count_limit, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "UDMABUF_CREATE_LIST with excessive count should fail (limit 1024)");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_count_limit, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	uint32_t excessive_count = 1025;
	struct udmabuf_uapi_item *items;
	int i;

	ATF_REQUIRE(snl_init_udmabuf(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	ATF_REQUIRE((items = calloc(excessive_count, sizeof(*items))) != NULL);

	for (i = 0; i < excessive_count; i++) {
		items[i].memfd = -1;
		items[i].size = getpagesize();
	}

	ATF_REQUIRE(create_udmabuf_list(&ss, family_id, items, excessive_count, UDMABUF_FLAGS_CLOEXEC) < 0);

	free(items);
	snl_free(&ss);
}

static bool
nl_set_sync(struct snl_state *ss, bool enable)
{
	int val = enable ? 1 : 0;
	socklen_t optlen = sizeof(val);
	if (setsockopt(ss->fd, SOL_NETLINK, NETLINK_SND_SYNC, &val, optlen) == -1) {
		return (false);
	}
	return (true);
}

static uint32_t
send_single_udmabuf_req(struct snl_state *ss, uint16_t family_id,
    struct udmabuf_uapi_item *item, uint32_t flags)
{
	size_t buf_size = 1024;
	struct nlmsghdr *nlh = calloc(1, buf_size);
	ATF_REQUIRE(nlh != NULL);

	nlh->nlmsg_type = family_id;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->nlmsg_seq = snl_get_seq(ss);
	nlh->nlmsg_len = sizeof(struct nlmsghdr) + sizeof(struct genlmsghdr);

	struct genlmsghdr *ghdr = (struct genlmsghdr *)(nlh + 1);
	ghdr->cmd = UDMABUF_CMD_CREATE_LIST;
	ghdr->version = 1;

	add_attr_u32(nlh, UDMABUF_ATTR_FLAGS, flags);

	/* 仅打包1个 item */
	struct nlattr *list_nla = add_nested_start(nlh, UDMABUF_ATTR_LISTS);
	struct nlattr *item_nla = add_nested_start(nlh, UDMABUF_ATTR_ITEM);
	add_attr_u32(nlh, UDMABUF_ATTR_MEMFD, item->memfd);
	add_attr_u64(nlh, UDMABUF_ATTR_OFFSET, item->offset);
	add_attr_u64(nlh, UDMABUF_ATTR_SIZE, item->size);
	add_nested_end(nlh, item_nla);
	add_nested_end(nlh, list_nla);

	uint32_t seq = nlh->nlmsg_seq;
	bool sent = snl_send(ss, nlh, nlh->nlmsg_len);
	free(nlh);
	
	ATF_REQUIRE(sent);
	return seq;
}

ATF_TC(nl_mixed_sync_async);
ATF_TC_HEAD(nl_mixed_sync_async, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Send multiple individual requests mixing NETLINK_SND_SYNC on and off");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_mixed_sync_async, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	
	const int NUM_REQS = 10;
	struct udmabuf_uapi_item items[NUM_REQS];
	uint32_t seqs[NUM_REQS];
	size_t size_per_fd = getpagesize() * 4;

	ATF_REQUIRE(snl_init(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);

	for (int i = 0; i < NUM_REQS; i++) {
		items[i].memfd = init_memfd(size_per_fd, false);
		items[i].offset = 0;
		items[i].size = size_per_fd;

		bool use_sync = (i % 2 == 0);
		ATF_REQUIRE(nl_set_sync(&ss, use_sync));

		seqs[i] = send_single_udmabuf_req(&ss, family_id, &items[i], UDMABUF_FLAGS_CLOEXEC);
	}

	for (int i = 0; i < NUM_REQS; i++) {
		bool was_sync = (i % 2 == 0);
		
		int fd_or_err = nl_get_reply_fd(&ss, seqs[i], family_id);

		if (was_sync) {
			ATF_REQUIRE_MSG(fd_or_err >= 0,
			    "Request %d (SYNC) should have succeeded, but failed with %d", i, fd_or_err);
			
			close(fd_or_err);
		} else {
			ATF_REQUIRE_MSG(fd_or_err < 0,
			    "Request %d (ASYNC) should have failed due to missing process context, but got fd %d", i, fd_or_err);
		}

		close(items[i].memfd);
	}

	snl_free(&ss);
}
ATF_TC(nl_sync_barrier_ordering);
ATF_TC_HEAD(nl_sync_barrier_ordering, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that a SYNC request strictly waits for preceding ASYNC requests to finish");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}
ATF_TC_BODY(nl_sync_barrier_ordering, tc)
{
	struct snl_state ss;
	uint16_t family_id;
	
	const int ASYNC_COUNT = 50;
	const int TOTAL_REQS = ASYNC_COUNT + 1; /* 4 ASYNC + 1 SYNC */
	struct udmabuf_uapi_item items[TOTAL_REQS];
	uint32_t seqs[TOTAL_REQS];
	size_t size_per_fd = getpagesize() * 4;

	ATF_REQUIRE(snl_init(&ss, NETLINK_GENERIC));
	ATF_REQUIRE((family_id = snl_get_genl_family(&ss, UDMABUF_FAMILY_NAME)) != 0);
	int small_buf = 2048;
	setsockopt(ss.fd, SOL_SOCKET, SO_RCVBUF, &small_buf, sizeof(small_buf));
	int large_snd = 1024 * 1024; // 1MB
	setsockopt(ss.fd, SOL_SOCKET, SO_SNDBUF, &large_snd, sizeof(large_snd));

	ATF_REQUIRE(nl_set_sync(&ss, false));
	for (int i = 0; i < ASYNC_COUNT; i++) {
		items[i].memfd = init_memfd(size_per_fd, false);
		items[i].offset = 0;
		items[i].size = size_per_fd;
		seqs[i] = send_single_udmabuf_req(&ss, family_id, &items[i], UDMABUF_FLAGS_CLOEXEC);
	}

	ATF_REQUIRE(nl_set_sync(&ss, true));
	int sync_idx = ASYNC_COUNT;
	items[sync_idx].memfd = init_memfd(size_per_fd, false);
	items[sync_idx].offset = 0;
	items[sync_idx].size = size_per_fd;
	seqs[sync_idx] = send_single_udmabuf_req(&ss, family_id, &items[sync_idx], UDMABUF_FLAGS_CLOEXEC);

	for (int i = 0; i < TOTAL_REQS; i++) {
		int fd_or_err = nl_get_reply_fd(&ss, seqs[i], family_id);

		if (i < ASYNC_COUNT) {
			ATF_REQUIRE_MSG(fd_or_err < 0,
			    "Async request %d should have failed, got fd %d", i, fd_or_err);
		} else {
			ATF_REQUIRE_MSG(fd_or_err >= 0,
			    "Sync barrier request should have succeeded, failed with %d", fd_or_err);
			close(fd_or_err);
		}
		close(items[i].memfd);
	}

	snl_free(&ss);
}
/* ------------------------------------------------------------------ */
/* Test Program                                                       */
/* ------------------------------------------------------------------ */

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, nl_single_item);
	ATF_TP_ADD_TC(tp, nl_multiple_items);
	ATF_TP_ADD_TC(tp, nl_offset_in_item);
	ATF_TP_ADD_TC(tp, nl_without_cloexec);
	ATF_TP_ADD_TC(tp, nl_with_cloexec);
	ATF_TP_ADD_TC(tp, nl_invalid_memfd);
	ATF_TP_ADD_TC(tp, nl_misaligned_offset_in_item);
	ATF_TP_ADD_TC(tp, nl_count_limit);
	ATF_TP_ADD_TC(tp, nl_mixed_sync_async);
	ATF_TP_ADD_TC(tp, nl_sync_barrier_ordering);

	return (atf_no_error());
}