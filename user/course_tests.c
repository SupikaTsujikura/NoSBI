#include <string.h>

#include <error.h>

#include "fd.h"
#include "lib.h"

#define CHECK(cond, msg)                                                                           \
	do {                                                                                         \
		if (!(cond)) {                                                                       \
			user_panic("course test failed: %s", msg);                                   \
		}                                                                                    \
	} while (0)

static void mark(const char *name) {
	debugf("[course:%s-ok]", name);
}

static void test_lab4_4_fork(void) {
	int a = 1;
	int pid = fork();

	CHECK(pid >= 0, "fork parent");
	if (pid == 0) {
		a += 4;
		for (int i = 0; i < 8; i++) {
			syscall_yield();
		}
		CHECK(a == 5, "fork child private data");
		exit();
	}
	for (int i = 0; i < 8; i++) {
		syscall_yield();
	}
	CHECK(a == 1, "fork parent private data");
	wait((u_long)pid);
	mark("lab4_4_fork");
}

static void test_lab4_5_ipc_page(void) {
	static int shared[1024] __attribute__((aligned(PAGE_SIZE)));
	static int recvbuf[1024] __attribute__((aligned(PAGE_SIZE)));
	u_long who = 0;
	u_long val = 0;
	u_long perm = 0;
	int child;

	for (int i = 0; i < 1024; i++) {
		shared[i] = 0;
		recvbuf[i] = 0;
	}
	child = fork();
	CHECK(child >= 0, "ipc fork");
	if (child == 0) {
		int r = ipc_recv(&who, &val, recvbuf, &perm);

		CHECK(r == 0, "ipc child recv");
		CHECK(val == 2026, "ipc value");
		CHECK((perm & PTE_LIBRARY) != 0, "ipc perm library");
		CHECK(recvbuf[0] == 2026 && recvbuf[100] != 0, "ipc page data");
		recvbuf[300] = 41036;
		ipc_send(who, 0, recvbuf, PTE_D | PTE_LIBRARY);
		exit();
	}
	shared[0] = 2026;
	shared[100] = syscall_getenvid();
	CHECK(ipc_send((u_long)child, 2026, shared, PTE_D | PTE_LIBRARY) == 0,
	      "ipc send page");
	CHECK(ipc_recv(&who, &val, recvbuf, &perm) == 0, "ipc parent recv page");
	CHECK(who == (u_long)child, "ipc sender");
	CHECK(recvbuf[300] == 41036, "ipc reply page");
	wait((u_long)child);
	mark("lab4_5_ipc_page");
}

static void test_lab5_1_dev_validation(void) {
	u32 word = 0;
	char byte = 0;

	CHECK(syscall_read_dev(&byte, 0x0fffffffUL, 1) == -E_INVAL, "read dev low bad");
	CHECK(syscall_write_dev(&byte, 0x0fffffffUL, 1) == -E_INVAL, "write dev low bad");
	CHECK(syscall_read_dev(&word, UART0_BASE + 0x101UL, 4) == -E_INVAL,
	      "read dev uart overflow");
	CHECK(syscall_write_dev(&word, UART0_BASE + 0x1fUL, 8) == -E_INVAL,
	      "write dev unaligned");
	CHECK(syscall_read_dev(&word, VIRTIO0_BASE + 8 * 0x1000UL, 4) == -E_INVAL,
	      "read dev virtio overflow");
	CHECK(syscall_write_dev(&word, VIRTIO0_BASE, 3) == -E_INVAL,
	      "write dev bad len");
	mark("lab5_1_dev_validation");
}

static void fill(char *buf, int len, char ch) {
	for (int i = 0; i < len; i++) {
		buf[i] = ch;
	}
}

static void test_lab5_file_rw(void) {
	char buf[600];
	const char *path = "/tmp/course/newmotd";
	const char *msg = "This is the NEW message of the day!\n";
	int fd;
	int n;

	(void)mkdir("/tmp");
	(void)mkdir("/tmp/course");
	fd = open(path, FS_OPEN_CREATE | FS_OPEN_TRUNC);
	CHECK(fd >= 0, "open create");
	CHECK(write(fd, msg, strlen(msg) + 1) == (int)strlen(msg) + 1, "write msg");
	CHECK(seek(fd, 0) == 0, "seek");
	n = read(fd, buf, sizeof(buf));
	CHECK(n == (int)strlen(msg) + 1, "read msg len");
	CHECK(strcmp(buf, msg) == 0, "read msg data");
	CHECK(ftruncate(fd, 0) == 0, "truncate");
	CHECK(seek(fd, 0) == 0, "seek after trunc");
	fill(buf, 511, 'A');
	for (int i = 0; i < 32; i++) {
		fill(buf, 511, (char)('a' + (i % 26)));
		CHECK(write(fd, buf, 511) == 511, "large write");
	}
	CHECK(close(fd) == 0, "close large");

	fd = open(path, 0);
	CHECK(fd >= 0, "reopen large");
	for (int i = 0; i < 32; i++) {
		n = read(fd, buf, 511);
		CHECK(n == 511, "large read");
		for (int j = 0; j < n; j++) {
			CHECK(buf[j] == (char)('a' + (i % 26)), "large read data");
		}
	}
	CHECK(close(fd) == 0, "close read");
	CHECK(remove(path) == 0, "remove file");
	CHECK(open(path, 0) < 0, "open removed");
	mark("lab5_file_rw");
}

static void test_lab5_fd_limit(void) {
	int fds[FS_MAX_FD];
	const char *path = "/tmp/course/fd.txt";
	int count = 0;
	int fd;

	fd = open(path, FS_OPEN_CREATE | FS_OPEN_TRUNC);
	CHECK(fd >= 0, "fd seed open");
	CHECK(write(fd, "fd", 2) == 2, "fd seed write");
	CHECK(close(fd) == 0, "fd seed close");

	for (int i = 0; i < FS_MAX_FD; i++) {
		fds[i] = open(path, 0);
		if (fds[i] < 0) {
			break;
		}
		count++;
	}
	CHECK(count > 0, "fd open many");
	CHECK(open(path, 0) < 0, "fd exhausted");
	for (int i = count - 1; i >= 0; i--) {
		CHECK(close(fds[i]) == 0, "fd close many");
	}
	CHECK(open(path, 0) == 0, "fd low reuse");
	CHECK(close(0) == 0, "fd close zero");
	(void)remove(path);
	mark("lab5_fd_limit");
}

static void test_lab6_pipe(void) {
	const char *msg = "Now is the time for all good men to come to the aid of their party.";
	char buf[128];
	int p[2];
	int pid;
	int n;

	CHECK(pipe(p) == 0, "pipe");
	pid = fork();
	CHECK(pid >= 0, "pipe fork");
	if (pid == 0) {
		close(p[1]);
		n = readn(p[0], buf, sizeof(buf) - 1);
		CHECK(n == (int)strlen(msg), "pipe read len");
		buf[n] = '\0';
		CHECK(strcmp(buf, msg) == 0, "pipe read data");
		close(p[0]);
		exit();
	}
	close(p[0]);
	CHECK(write(p[1], msg, strlen(msg)) == (int)strlen(msg), "pipe write");
	close(p[1]);
	wait((u_long)pid);
	mark("lab6_pipe");
}

static void test_lab6_ptelibrary(void) {
	const char *msg = "hello world!\n";
	char *tmp = (char *)0x30000000UL;
	int pid;

	CHECK(syscall_mem_alloc(0, tmp, PTE_D | PTE_LIBRARY) == 0,
	      "library page alloc");
	strcpy(tmp, "parent\n");
	pid = fork();
	CHECK(pid >= 0, "library fork");
	if (pid == 0) {
		strcpy(tmp, msg);
		exit();
	}
	wait((u_long)pid);
	CHECK(strcmp(tmp, msg) == 0, "library page shared by fork");
	CHECK(syscall_mem_unmap(0, tmp) == 0, "library unmap");
	mark("lab6_ptelibrary");
}

void user_main(long arg, char **argv) {
	(void)arg;
	(void)argv;
	debugf("[course:start]");
	test_lab4_4_fork();
	test_lab4_5_ipc_page();
	test_lab5_1_dev_validation();
	test_lab5_file_rw();
	test_lab5_fd_limit();
	test_lab6_pipe();
	test_lab6_ptelibrary();
	debugf("[course:all-ok]");
	exit();
}
