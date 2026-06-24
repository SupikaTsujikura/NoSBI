#include <arch/vm.h>
#include <env.h>
#include <error.h>
#include <string.h>

#include "lib.h"

static int fork_once_done;
static int spawn_once_done;
static volatile int cow_value;
static char fs_big_chunk[5200];

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
	struct FsStat fsstat;
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

	if (syscall_fs_stat("/bin/demo", &fsstat) == 0) {
		puts_user("[elf-size=");
		puthex_user(fsstat.size);
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
	    syscall_fs_stat("/tmp/renamed.txt", &fsstat) == 0) {
		puts_user("[fs-rename-ok]");
	}
	for (int i = 0; i < 8; i++) {
		struct Stat ust;
		int r = list(i, buf, sizeof(buf), &ust);

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

	fd = open("/hello.txt", 0);
	if (fd >= 0) {
		void *blk = 0;
		int mr = read_map(fd, 0, &blk);

		if (mr > 5 && blk != 0 && ((char *)blk)[0] == 'h' &&
		    ((char *)blk)[1] == 'e') {
			puts_user("[read-map-ok]");
		}
		(void)close(fd);
	}

	fd = open("/tmp/mapdirty.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd >= 0) {
		void *blk = 0;

		(void)write(fd, "abcde", 5);
		if (read_map(fd, 0, &blk) > 0 && blk != 0) {
			((char *)blk)[0] = 'M';
			((char *)blk)[1] = 'A';
			((char *)blk)[2] = 'P';
			if (dirty(fd, 0, 3) == 0) {
				(void)seek(fd, 0);
				n = read(fd, buf, 5);
				if (n == 5 && buf[0] == 'M' && buf[1] == 'A' &&
				    buf[2] == 'P' && buf[3] == 'd' && buf[4] == 'e') {
					puts_user("[map-dirty-ok]");
				}
			}
		}
		(void)close(fd);
		(void)remove("/tmp/mapdirty.txt");
	}

	fd = open("/tmp/bigmap.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd >= 0) {
		struct Stat ust;
		void *blk = 0;

		for (int i = 0; i < (int)sizeof(fs_big_chunk); i++) {
			fs_big_chunk[i] = (char)('a' + (i % 23));
		}
		if (write(fd, fs_big_chunk, sizeof(fs_big_chunk)) == (int)sizeof(fs_big_chunk) &&
		    read_map(fd, PAGE_SIZE, &blk) > 0 && blk != 0) {
			((char *)blk)[0] = 'P';
			((char *)blk)[1] = '2';
			if (dirty(fd, PAGE_SIZE, 2) == 0) {
				(void)seek(fd, PAGE_SIZE);
				n = read(fd, buf, 4);
				if (n == 4 && buf[0] == 'P' && buf[1] == '2') {
					puts_user("[map-page2-ok]");
				}
			}
		}
		if (ftruncate(fd, 1024) == 0 && fstat(fd, &ust) == 0 &&
		    ust.st_size == 1024) {
			puts_user("[truncate-cache-ok]");
		}
		(void)close(fd);
		(void)remove("/tmp/bigmap.txt");
	}

	fd = open("/tmp/fdshare.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd >= 0) {
		int fd2;

		(void)write(fd, "xyz", 3);
		(void)seek(fd, 0);
		fd2 = dup(fd, 8);
		if (fd2 == 8 && read(fd2, buf, 2) == 2 && read(fd, buf + 2, 1) == 1) {
			buf[3] = '\0';
			if (buf[0] == 'x' && buf[1] == 'y' && buf[2] == 'z') {
				puts_user("[fd-share-ok]");
			}
		}
		(void)close(fd2);
		(void)close(fd);
		(void)remove("/tmp/fdshare.txt");
	}

	fd = open("/tmp/append.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd >= 0) {
		(void)write(fd, "aa", 2);
		(void)close(fd);
		fd = open("/tmp/append.txt", FS_OPEN_APPEND);
		if (fd >= 0) {
			(void)seek(fd, 0);
			(void)write(fd, "bb", 2);
			(void)seek(fd, 0);
			n = read(fd, buf, 4);
			if (n == 4 && buf[0] == 'a' && buf[1] == 'a' &&
			    buf[2] == 'b' && buf[3] == 'b') {
				puts_user("[append-ok]");
			}
			(void)close(fd);
		}
		(void)remove("/tmp/append.txt");
	}

	(void)mkdir("/tmp/tree");
	(void)mkdir("/tmp/tree/sub");
	fd = open("/tmp/tree/sub/file.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd >= 0) {
		(void)write(fd, "tree", 4);
		(void)close(fd);
	}
	if (remove("/tmp/tree") < 0 &&
	    rename("/tmp/tree", "/tmp/tree2") == 0 &&
	    stat("/tmp/tree2/sub/file.txt", &(struct Stat){0}) == 0) {
		puts_user("[tree-rename-ok]");
	}
	{
		int saw_sub = 0;
		int saw_nested = 0;

		for (int i = 0; i < 8; i++) {
			int lr = listdir("/tmp/tree2", i, buf, sizeof(buf), 0);

			if (lr <= 0) {
				break;
			}
			if (strcmp(buf, "/tmp/tree2/sub") == 0) {
				saw_sub = 1;
			}
			if (strcmp(buf, "/tmp/tree2/sub/file.txt") == 0) {
				saw_nested = 1;
			}
		}
		if (saw_sub && !saw_nested) {
			puts_user("[listdir-child-ok]");
		}
	}
	if (remove("/tmp/tree2/sub/file.txt") == 0 &&
	    remove("/tmp/tree2/sub") == 0 &&
	    remove("/tmp/tree2") == 0 &&
	    stat("/tmp/tree2/sub/file.txt", &(struct Stat){0}) < 0) {
		puts_user("[tree-remove-ok]");
	}

	(void)mkdir("/tmp/dirfd");
	fd = open("/tmp/dirfd/one.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd >= 0) {
		(void)write(fd, "1", 1);
		(void)close(fd);
	}
	fd = open("/tmp/dirfd/two.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd >= 0) {
		(void)write(fd, "2", 1);
		(void)close(fd);
	}
	fd = open("/tmp/dirfd", 0);
	if (fd >= 0) {
		struct FsDirent ents[4];
		int bytes = getdents(fd, ents, sizeof(ents));
		int saw_one = 0;
		int saw_two = 0;

		for (int i = 0; i < bytes / (int)sizeof(ents[0]); i++) {
			if (strcmp(ents[i].name, "/tmp/dirfd/one.txt") == 0) {
				saw_one = 1;
			}
			if (strcmp(ents[i].name, "/tmp/dirfd/two.txt") == 0) {
				saw_two = 1;
			}
		}
		if (saw_one && saw_two) {
			puts_user("[dirfd-getdents-ok]");
		}
		(void)close(fd);
	}
	(void)remove("/tmp/dirfd/one.txt");
	(void)remove("/tmp/dirfd/two.txt");
	(void)remove("/tmp/dirfd");

	(void)mkdir("/tmp/cwd");
	if (chdir("/tmp/cwd") == 0) {
		fd = open("rel.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
		if (fd >= 0) {
			(void)write(fd, "cwd", 3);
			(void)close(fd);
		}
		fd = open("./rel.txt", 0);
		if (fd >= 0) {
			n = read(fd, buf, 3);
			(void)close(fd);
			if (n == 3 && buf[0] == 'c' && buf[1] == 'w' &&
			    buf[2] == 'd' && getcwd(buf, sizeof(buf)) == 0 &&
			    strcmp(buf, "/tmp/cwd") == 0) {
				puts_user("[cwd-relative-ok]");
			}
		}
		(void)chdir("/");
	}
	(void)remove("/tmp/cwd/rel.txt");
	(void)remove("/tmp/cwd");

	fd = open("/tmp/link-src.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd >= 0) {
		(void)write(fd, "linked", 6);
		(void)close(fd);
	}
	if (link("/tmp/link-src.txt", "/tmp/link-alias.txt") == 0) {
		struct Stat lst;

		fd = open("/tmp/link-alias.txt", 0);
		n = fd >= 0 ? read(fd, buf, 6) : -1;
		if (fd >= 0) {
			(void)close(fd);
		}
		if (n == 6 && stat("/tmp/link-src.txt", &lst) == 0 &&
		    lst.st_nlink == 2 && remove("/tmp/link-src.txt") == 0) {
			fd = open("/tmp/link-alias.txt", 0);
			n = fd >= 0 ? read(fd, buf, 6) : -1;
			if (fd >= 0) {
				(void)close(fd);
			}
			if (n == 6) {
				puts_user("[hardlink-ok]");
			}
		}
	}
	if (symlink("/tmp/link-alias.txt", "/tmp/link-sym.txt") == 0) {
		char target[FS_PATH_MAX];

		fd = open("/tmp/link-sym.txt", 0);
		n = fd >= 0 ? read(fd, buf, 6) : -1;
		if (fd >= 0) {
			(void)close(fd);
		}
		if (n == 6 && readlink("/tmp/link-sym.txt", target, sizeof(target)) > 0 &&
		    strcmp(target, "/tmp/link-alias.txt") == 0) {
			puts_user("[symlink-ok]");
		}
	}
	if (chmod("/tmp/link-alias.txt", 0600) == 0) {
		struct Stat mst;

		if (stat("/tmp/link-alias.txt", &mst) == 0 && mst.st_mode == 0600) {
			puts_user("[chmod-ok]");
		}
	}
	(void)remove("/tmp/link-sym.txt");
	(void)remove("/tmp/link-alias.txt");

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

static void ipc_smoke(void) {
	int child;
	u_long from = 0;
	u_long val = 0;
	u_long perm = 1;
	u_long parent = syscall_getenvid();

	child = fork();
	if (child < 0) {
		syscall_panic("ipc fork failed");
	}
	if (child == 0) {
		(void)ipc_send(parent, 0x55, 0, 0);
		(void)syscall_env_destroy(0);
		for (;;) {
			syscall_yield();
		}
	}
	if (ipc_recv(&from, &val, 0, &perm) == 0 && from != 0 && val == 0x55 &&
	    perm == 0) {
		puts_user("[ipc-ok]");
	}
}

static void dev_syscall_smoke(void) {
	char byte = 0;

	if (syscall_read_dev(&byte, UART0_BASE, 3) == -E_INVAL &&
	    syscall_write_dev(&byte, USER_TOP, 1) == -E_INVAL) {
		puts_user("[dev-check-ok]");
	}
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
		dev_syscall_smoke();
	}
	for (int i = 0; i < 8; i++) {
		syscall_putchar(ch);
		syscall_yield();
	}
	if (ch == 'A') {
		spawn_smoke();
		ipc_smoke();
		fork_cow_smoke();
	}
	puts_user("[user-c-done]");
	for (;;) {
		syscall_yield();
	}
}
