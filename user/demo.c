#include <arch/vm.h>
#include <env.h>
#include <error.h>

#include "lib.h"

static int fork_once_done;
static int spawn_once_done;
static volatile int cow_value;

static u_long user_strlen(const char *s) {
	u_long len = 0;

	while (s[len] != '\0') {
		len++;
	}
	return len;
}

static void puts_user(const char *s) {
	(void)syscall_print_cons(s, user_strlen(s));
}

static void puthex_user(unsigned long x) {
	const char *digits = "0123456789abcdef";

	puts_user("0x");
	for (int i = (int)(sizeof(x) * 2) - 1; i >= 0; i--) {
		syscall_putchar(digits[(x >> (i * 4)) & 0xf]);
	}
}

static void fs_smoke(void) {
	char buf[80];
	struct FsStat stat;
	int fd;
	int n;

	fd = syscall_fs_open("/hello.txt");
	if (fd < 0) {
		puts_user("[fs-open-fail]");
		return;
	}
	n = syscall_fs_read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		puts_user("[fs-read-fail]");
		(void)syscall_fs_close(fd);
		return;
	}
	buf[n] = '\0';
	puts_user("[fs-read:");
	puts_user(buf);
	puts_user("]");
	(void)syscall_fs_close(fd);

	if (syscall_fs_stat("/bin/demo", &stat) == 0) {
		puts_user("[elf-size=");
		puthex_user(stat.size);
		puts_user("]");
	}

	fd = syscall_fs_open_flags("/tmp/out.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd < 0) {
		puts_user("[fs-create-fail]");
		return;
	}
	if (syscall_fs_write(fd, "abc", 3) != 3 ||
	    syscall_fs_write(fd, "123", 3) != 3 ||
	    syscall_fs_seek(fd, 0) < 0) {
		puts_user("[fs-write-fail]");
		(void)syscall_fs_close(fd);
		return;
	}
	n = syscall_fs_read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		puts_user("[fs-reread-fail]");
		(void)syscall_fs_close(fd);
		return;
	}
	buf[n] = '\0';
	puts_user("[fs-write-read:");
	puts_user(buf);
	puts_user("]");
	(void)syscall_fs_close(fd);

	if (syscall_fs_rename("/tmp/out.txt", "/tmp/renamed.txt") == 0 &&
	    syscall_fs_stat("/tmp/renamed.txt", &stat) == 0) {
		puts_user("[fs-rename-ok]");
	}
	for (int i = 0; i < 8; i++) {
		int r = syscall_fs_list(i, buf, sizeof(buf), &stat);

		if (r == 0) {
			break;
		}
		if (r > 0 && buf[0] == '/' && buf[1] == 't') {
			puts_user("[fs-list:");
			puts_user(buf);
			puts_user("]");
		}
	}
	if (syscall_fs_unlink("/tmp/renamed.txt") == 0 &&
	    syscall_fs_open("/tmp/renamed.txt") < 0) {
		puts_user("[fs-unlink-ok]");
	}

	fd = syscall_fs_open("/disk.txt");
	if (fd >= 0) {
		n = syscall_fs_read(fd, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			puts_user("[ext4-read:");
			puts_user(buf);
			puts_user("]");
		}
		(void)syscall_fs_close(fd);
	}
}

static void pipe_smoke(void) {
	int pfd[2];
	char buf[16];
	int n;

	if (pipe(pfd) < 0) {
		puts_user("[pipe-create-fail]");
		return;
	}
	if (write(pfd[1], "pipe-ok", 7) != 7) {
		puts_user("[pipe-write-fail]");
		(void)close(pfd[0]);
		(void)close(pfd[1]);
		return;
	}
	n = read(pfd[0], buf, sizeof(buf) - 1);
	if (n < 0) {
		puts_user("[pipe-read-fail]");
		(void)close(pfd[0]);
		(void)close(pfd[1]);
		return;
	}
	buf[n] = '\0';
	puts_user("[pipe-read:");
	puts_user(buf);
	puts_user("]");
	(void)close(pfd[0]);
	(void)close(pfd[1]);
}

static void fork_cow_smoke(void) {
	int child;

	if (fork_once_done) {
		return;
	}
	fork_once_done = 1;
	cow_value = 7;
	child = fork();
	if (child < 0) {
		syscall_panic("fork failed");
	}
	if (child == 0) {
		puts_user("[fork-child]");
		if (cow_value == 7) {
			puts_user("[child-sees-parent-value]");
		}
		cow_value = 99;
		if (cow_value == 99) {
			puts_user("[child-cow-write]");
		}
		(void)syscall_env_destroy(0);
		for (;;) {
			syscall_yield();
		}
	}
	puts_user("[fork-ok]");
	for (int i = 0; i < 8; i++) {
		syscall_yield();
	}
	if (cow_value == 7) {
		puts_user("[parent-cow-ok]");
	} else {
		syscall_panic("parent cow value changed");
	}
}

static void spawn_smoke(void) {
	int child;
	char *argv[] = {"argvtest", "one", "two", 0};

	if (spawn_once_done) {
		return;
	}
	spawn_once_done = 1;
	child = syscall_spawn("/bin/demo", 'S');
	if (child < 0) {
		syscall_panic("spawn /bin/demo failed");
	}
	puts_user("[spawn-ok]");
	child = spawn("/bin/argvtest", argv);
	if (child < 0) {
		syscall_panic("spawn /bin/argvtest failed");
	}
	puts_user("[spawn-argv-ok]");
}

void user_main(long arg, char **argv) {
	(void)argv;
	int ch = (int)arg;

	puts_user("[user-c-start]");
	if (ch == 'A') {
		fs_smoke();
		pipe_smoke();
	}
	for (int i = 0; i < 8; i++) {
		syscall_putchar(ch);
		syscall_yield();
	}
	if (ch == 'A') {
		spawn_smoke();
		fork_cow_smoke();
	}
	puts_user("[user-c-done]");
	for (;;) {
		syscall_yield();
	}
}
