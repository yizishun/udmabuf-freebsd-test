#include <atf-c/tc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <atf-c.h>

/*
 * Test Case 1: Open and Close
 * Tries to open the /dev/udmabuf0 device and close it.
 * Skips if the device node does not exist.
 */
ATF_TC(open_close);
ATF_TC_HEAD(open_close, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks if /dev/udmabuf0 can be opened and closed");
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(open_close, tc)
{
	int fd;

	fd = open("/dev/udmabuf0", O_RDWR);
	if (fd == -1) {
		if (errno == ENOENT)
			atf_tc_fail("Device /dev/udmabuf0 not found; module might not be loaded");
		else
			atf_tc_fail("Failed to open /dev/udmabuf0: %s", strerror(errno));
	}

	if (close(fd) == -1)
		atf_tc_fail("Failed to close /dev/udmabuf0: %s", strerror(errno));
}

/*
 * Test Case 2: Basic Info
 * Verifies that it is a character device.
 * This demonstrates how to chain checks on an open file descriptor.
 */
ATF_TC(basic_info);
ATF_TC_HEAD(basic_info, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks device file statistics");
	atf_tc_set_md_var(tc, "require.user", "root"); /* Often needed for device access */
	atf_tc_set_md_var(tc, "require.kmods", "dmabuf udmabuf");
}

ATF_TC_BODY(basic_info, tc)
{
	int fd;
	struct stat st;

	fd = open("/dev/udmabuf0", O_RDWR);
	if (fd == -1) {
		if (errno == ENOENT)
			atf_tc_fail("Device /dev/udmabuf0 not found");
		atf_tc_fail("Open failed: %s", strerror(errno));
	}

	if (fstat(fd, &st) == -1) {
		close(fd);
		atf_tc_fail("fstat failed: %s", strerror(errno));
	}

	/* Check if it is a character device */
	if (!S_ISCHR(st.st_mode)) {
		close(fd);
		atf_tc_fail("/dev/udmabuf0 is not a character device");
	}

	close(fd);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, open_close);
	ATF_TP_ADD_TC(tp, basic_info);

	return (atf_no_error());
}
