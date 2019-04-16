#include "userfs.h"
#include "unit.h"
#include <limits.h>
#include <string.h>

static void
test_open(void)
{
	unit_test_start();
	
	int fd = ufs_open("file", 0);
	unit_check(fd == -1, "error when no such file");
	unit_check(ufs_errno() == UFS_ERR_NO_FILE, "errno is 'no_file'");

	fd = ufs_open("file", UFS_CREATE);
	unit_check(fd != -1, "use 'create' now");
	unit_check(ufs_close(fd) == 0, "close immediately");

	fd = ufs_open("file", 0);
	unit_check(fd != -1, "now open works without 'create'");
	unit_fail_if(ufs_close(fd) != 0);

	fd = ufs_open("file", UFS_CREATE);
	unit_check(fd != -1, "'create' is not an error when file exists");

	int fd2 = ufs_open("file", 0);
	unit_check(fd2 != -1, "open second descriptor");
	unit_check(fd2 != fd, "it is not the same in value");
	unit_check(ufs_close(fd2) == 0, "close the second");

	unit_check(ufs_close(fd) == 0, "and the first");

	unit_check(ufs_delete("file") == 0, "deletion");
	unit_check(ufs_open("file", 0) == -1, "now 'create' is needed again");

	unit_test_finish();
}

static void
test_close(void)
{
	unit_test_start();

	unit_check(ufs_close(-1) == -1, "close invalid fd");
	unit_check(ufs_errno() == UFS_ERR_NO_FILE, "errno is set");

	unit_check(ufs_close(0) == -1, "close with seemingly normal fd");
	unit_fail_if(ufs_errno() != UFS_ERR_NO_FILE);

	unit_check(ufs_close(INT_MAX) == -1, "close with huge invalid fd");
	unit_fail_if(ufs_errno() != UFS_ERR_NO_FILE);

	int fd = ufs_open("file", UFS_CREATE);
	unit_fail_if(fd == -1);
	unit_check(ufs_close(fd) == 0, "close normal descriptor");
	unit_check(ufs_close(fd) == -1, "close it second time");
	unit_check(ufs_errno() == UFS_ERR_NO_FILE, "errno is set");

	unit_test_finish();
}

static void
test_io(void)
{
	unit_test_start();

	ssize_t rc = ufs_write(-1, NULL, 0);
	unit_check(rc == -1, "write into invalid fd");
	unit_check(ufs_errno() == UFS_ERR_NO_FILE, "errno is set");
	rc = ufs_write(0, NULL, 0);

	unit_check(rc == -1, "write into seemingly valid fd");
	unit_fail_if(ufs_errno() != UFS_ERR_NO_FILE);

	rc = ufs_read(-1, NULL, 0);
	unit_check(rc == -1, "read from invalid fd");
	unit_check(ufs_errno() == UFS_ERR_NO_FILE, "errno is set");
	rc = ufs_read(0, NULL, 0);

	unit_check(rc == -1, "read from seemingly valid fd");
	unit_fail_if(ufs_errno() != UFS_ERR_NO_FILE);

	int fd1 = ufs_open("file", UFS_CREATE);
	int fd2 = ufs_open("file", 0);
	unit_fail_if(fd1 == -1 || fd2 == -1);

	const char *data = "123";
	int size = strlen(data) + 1;
	unit_check(ufs_write(fd1, data, size) == size, "data is written");

	char buffer[2048];
	unit_check(ufs_read(fd2, buffer, sizeof(buffer)) == size,
		   "data is read");
	unit_check(memcmp(data, buffer, size) == 0, "the same data");

	ufs_close(fd1);
	ufs_close(fd2);
	ufs_delete("file");

	unit_test_finish();
}

static void
test_delete(void)
{
	unit_test_start();

	char c1, c2;
	int fd1 = ufs_open("file", UFS_CREATE);
	int fd2 = ufs_open("file", 0);
	int fd3 = ufs_open("file", 0);
	unit_fail_if(fd1 == -1 || fd2 == -1 || fd3 == -1);

	unit_check(ufs_delete("file") == 0,
		   "delete when opened descriptors exist");
	unit_check(ufs_write(fd2, "a", 1) == 1,
		   "write into an fd opened before deletion");
	unit_check(ufs_read(fd3, &c1, 1) == 1,
		   "read from another opened fd - it sees the data");
	unit_check(c1 == 'a', "exactly the same data");
	unit_check(ufs_write(fd3, "bc", 2) == 2,
		   "write into it and the just read data is not overwritten");

	unit_check(ufs_read(fd1, &c1, 1) == 1, "read from the first one");
	unit_check(ufs_read(fd1, &c2, 1) == 1, "read from the first one again");
	unit_check(c1 == 'a' && c2 == 'b', "it reads data in correct order");

	int fd4 = ufs_open("file", 0);
	unit_check(fd4 == -1, "the existing 'ghost' file is not visible "\
		   "anymore for new opens");
	unit_check(ufs_errno() == UFS_ERR_NO_FILE, "errno is set");

	fd4 = ufs_open("file", UFS_CREATE);
	unit_fail_if(fd4 == -1);
	unit_check(ufs_read(fd4, &c1, 1) == 0,
		   "the file is created back, no data");
	unit_check(ufs_read(fd1, &c2, 1) == 1, "but the ghost still lives");
	unit_check(c2 == 'c', "and gives correct data");

	unit_check(ufs_delete("file") == 0, "delete it again");

	unit_fail_if(ufs_close(fd1) != 0);
	unit_fail_if(ufs_close(fd2) != 0);
	unit_fail_if(ufs_close(fd3) != 0);
	unit_fail_if(ufs_close(fd4) != 0);

	unit_test_start();
}

int
main(void)
{
	unit_test_start();

	test_open();
	test_close();
	test_io();
	test_delete();

	unit_test_finish();
	return 0;
}
