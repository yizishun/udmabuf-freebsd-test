/*-
 * Copyright (c) 2026 Zishun Yi.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _SYS_UDMABUF_H
#define _SYS_UDMABUF_H

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define UDMABUF_FLAGS_CLOEXEC	0x01

struct udmabuf_create {
	uint32_t memfd;
	uint32_t flags;
	uint64_t offset;
	uint64_t size;
};

struct udmabuf_create_item {
	uint32_t memfd;
	uint32_t __pad;
	uint64_t offset;
	uint64_t size;
};

struct udmabuf_create_list {
	uint32_t flags;
	uint32_t count;
	struct udmabuf_create_item list[];
};

#define UDMABUF_CREATE       _IOW('u', 0x42, struct udmabuf_create)
#define UDMABUF_CREATE_LIST  _IOW('u', 0x43, struct udmabuf_create_list)

#endif /* _SYS_UDMABUF_H */
