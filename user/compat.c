#include <stdarg.h>
#include <env.h>
#include <error.h>
#include <fs.h>
#include <string.h>

#include "lib.h"

static void stat_from_fs(struct Stat *dst, const struct FsStat *src) {
	if (dst == 0 || src == 0) {
		return;
	}
	dst->st_size = src->size;
	dst->st_type = src->type;
	dst->st_reserved = src->reserved;
}

int fork(void) {
	return syscall_fork();
}

void exit(void) {
	(void)syscall_env_destroy(0);
	for (;;) {
		syscall_yield();
	}
}

void wait(u_long envid) {
	for (;;) {
		int status = syscall_env_status(envid);

		if (status == ENV_FREE || status < 0) {
			return;
		}
		syscall_yield();
	}
}

int ipc_send(u_long whom, u_long val, const void *srcva, u_long perm) {
	int r;

	do {
		r = syscall_ipc_try_send(whom, val, srcva, perm);
		if (r == -E_IPC_NOT_RECV) {
			syscall_yield();
		}
	} while (r == -E_IPC_NOT_RECV);
	return r;
}

int ipc_recv(u_long *whom, u_long *val, void *dstva, u_long *perm) {
	long r = syscall_ipc_recv(dstva);

	if (r < 0) {
		return (int)r;
	}
	(void)syscall_ipc_info(whom, perm);
	if (val != 0) {
		*val = (u_long)r;
	}
	return 0;
}

int open(const char *path, int flags) {
	return syscall_fs_open_flags(path, flags);
}

int read(int fd, void *buf, u_long n) {
	int r;

	do {
		r = syscall_fs_read(fd, buf, n);
		if (r == -E_IPC_NOT_RECV) {
			syscall_yield();
		}
	} while (r == -E_IPC_NOT_RECV);
	return r;
}

int readn(int fd, void *buf, u_long n) {
	u_long done = 0;

	while (done < n) {
		int r = read(fd, (char *)buf + done, n - done);

		if (r < 0) {
			return r;
		}
		if (r == 0) {
			break;
		}
		done += (u_long)r;
	}
	return (int)done;
}

int write(int fd, const void *buf, u_long n) {
	int r;

	if (fd == 1 || fd == 2) {
		return syscall_print_cons(buf, n) < 0 ? -E_INVAL : (int)n;
	}
	do {
		r = syscall_fs_write(fd, buf, n);
		if (r == -E_IPC_NOT_RECV) {
			syscall_yield();
		}
	} while (r == -E_IPC_NOT_RECV);
	return r;
}

int seek(int fd, u_long offset) {
	return syscall_fs_seek(fd, offset);
}

int stat(const char *path, struct Stat *statbuf) {
	struct FsStat fsstat;
	int r = syscall_fs_stat(path, &fsstat);

	if (r < 0) {
		return r;
	}
	stat_from_fs(statbuf, &fsstat);
	return 0;
}

int fstat(int fd, struct Stat *statbuf) {
	struct FsStat fsstat;
	int r = syscall_fs_fstat(fd, &fsstat);

	if (r < 0) {
		return r;
	}
	stat_from_fs(statbuf, &fsstat);
	return 0;
}

int close(int fd) {
	return syscall_fs_close(fd);
}

void close_all(void) {
	for (int fd = 0; fd < FS_MAX_FD; fd++) {
		(void)close(fd);
	}
}

int dup(int oldfd, int newfd) {
	return syscall_fs_dup(oldfd, newfd);
}

int remove(const char *path) {
	return syscall_fs_unlink(path);
}

int rename(const char *old_path, const char *new_path) {
	return syscall_fs_rename(old_path, new_path);
}

int ftruncate(int fd, u_long size) {
	return syscall_fs_truncate(fd, size);
}

int sync(void) {
	return 0;
}

int pipe(int pfd[2]) {
	return syscall_pipe(pfd);
}

int spawn(char *prog, char **argv) {
	return syscall_spawn(prog, (long)argv);
}

int spawnl(char *prog, const char *arg0, ...) {
	char *argv[16];
	int argc = 0;
	va_list ap;

	if (arg0 != 0) {
		argv[argc++] = (char *)arg0;
	}
	va_start(ap, arg0);
	while (argc < (int)(sizeof(argv) / sizeof(argv[0])) - 1) {
		char *arg = va_arg(ap, char *);

		if (arg == 0) {
			break;
		}
		argv[argc++] = arg;
	}
	va_end(ap);
	argv[argc] = 0;
	return spawn(prog, argv);
}
