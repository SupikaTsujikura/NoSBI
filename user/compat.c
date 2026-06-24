#include <stdarg.h>
#include <env.h>
#include <error.h>
#include <fs.h>
#include <string.h>

#include "fd.h"
#include "fsreq.h"
#include "lib.h"

static int file_read(struct Fd *fd, void *buf, u_long n);
static int file_write(struct Fd *fd, const void *buf, u_long n);
static int file_close(struct Fd *fd);
static int file_stat(struct Fd *fd, struct Stat *statbuf);
static int file_seek(struct Fd *fd, u_long offset);
static int pipe_read(struct Fd *fd, void *buf, u_long n);
static int pipe_write(struct Fd *fd, const void *buf, u_long n);
static int pipe_close(struct Fd *fd);

struct Dev devfile = {
	.dev_id = DEV_FILE,
	.dev_name = "file",
	.dev_read = file_read,
	.dev_write = file_write,
	.dev_close = file_close,
	.dev_stat = file_stat,
	.dev_seek = file_seek,
};

struct Dev devpipe = {
	.dev_id = DEV_PIPE,
	.dev_name = "pipe",
	.dev_read = pipe_read,
	.dev_write = pipe_write,
	.dev_close = pipe_close,
	.dev_stat = file_stat,
	.dev_seek = 0,
};

static char user_cwd[FS_PATH_MAX] = "/";

static int append_component(char out[FS_PATH_MAX], const char *comp, u_long len) {
	u_long out_len;

	if (len == 0 || (len == 1 && comp[0] == '.')) {
		return 0;
	}
	if (len == 2 && comp[0] == '.' && comp[1] == '.') {
		out_len = strlen(out);
		if (out_len == 1) {
			return 0;
		}
		while (out_len > 1 && out[out_len - 1] != '/') {
			out_len--;
		}
		if (out_len > 1) {
			out_len--;
		}
		out[out_len] = '\0';
		return 0;
	}
	out_len = strlen(out);
	if (out_len != 1) {
		if (out_len + 1 >= FS_PATH_MAX) {
			return -E_INVAL;
		}
		out[out_len++] = '/';
	}
	if (out_len + len >= FS_PATH_MAX) {
		return -E_INVAL;
	}
	memcpy(out + out_len, comp, len);
	out[out_len + len] = '\0';
	return 0;
}

static int resolve_path(const char *path, char out[FS_PATH_MAX]) {
	char temp[FS_PATH_MAX * 2];
	u_long ti = 0;
	const char *p;

	if (path == 0 || path[0] == '\0') {
		return -E_INVAL;
	}
	if (path[0] != '/') {
		u_long cwd_len = strlen(user_cwd);

		if (cwd_len + 1 >= sizeof(temp)) {
			return -E_INVAL;
		}
		memcpy(temp, user_cwd, cwd_len);
		ti = cwd_len;
		if (ti > 1) {
			temp[ti++] = '/';
		}
	}
	for (p = path; *p != '\0'; p++) {
		if (ti + 1 >= sizeof(temp)) {
			return -E_INVAL;
		}
		temp[ti++] = *p;
	}
	temp[ti] = '\0';
	out[0] = '/';
	out[1] = '\0';
	p = temp;
	while (*p != '\0') {
		const char *start;

		while (*p == '/') {
			p++;
		}
		start = p;
		while (*p != '\0' && *p != '/') {
			p++;
		}
		if (append_component(out, start, (u_long)(p - start)) < 0) {
			return -E_INVAL;
		}
	}
	return 0;
}

static void stat_from_fs(struct Stat *dst, const struct FsStat *src) {
	if (dst == 0 || src == 0) {
		return;
	}
	dst->st_size = src->size;
	dst->st_type = src->type;
	dst->st_mode = src->mode;
	dst->st_nlink = src->nlink;
	dst->st_uid = src->uid;
	dst->st_gid = src->gid;
}

static int sync_kernel_offset(struct Fd *fd) {
	int backend = fd_backend(fd);

	if (backend < 0) {
		return backend;
	}
	return fsipc_seek(backend, fd_offset(fd));
}

static int file_read(struct Fd *fd, void *buf, u_long n) {
	int backend;
	int r;
	u_long done = 0;
	struct FsStat fsstat;

	backend = fd_backend(fd);
	if (backend < 0) {
		return backend;
	}
	if (fsipc_fstat(backend, &fsstat) == 0 && fsstat.type == FS_TYPE_DIR) {
		r = fsipc_getdents(backend, buf, n);
		if (r > 0) {
			fd_add_offset(fd, (u_long)r / sizeof(struct FsDirent));
		}
		return r;
	}
	if ((r = sync_kernel_offset(fd)) < 0) {
		return r;
	}
	while (done < n) {
		r = fsipc_read(backend, (char *)buf + done, n - done);
		if (r < 0) {
			return done == 0 ? r : (int)done;
		}
		if (r == 0) {
			break;
		}
		done += (u_long)r;
		fd_add_offset(fd, (u_long)r);
	}
	return (int)done;
}

static int file_write(struct Fd *fd, const void *buf, u_long n) {
	int backend;
	int r;
	u_long done = 0;
	struct FsStat fsstat;

	backend = fd_backend(fd);
	if (backend < 0) {
		return backend;
	}
	if (fsipc_fstat(backend, &fsstat) == 0 && fsstat.type == FS_TYPE_DIR) {
		return -E_INVAL;
	}
	if ((r = sync_kernel_offset(fd)) < 0) {
		return r;
	}
	while (done < n) {
		r = fsipc_write(backend, (const char *)buf + done, n - done);
		if (r < 0) {
			return done == 0 ? r : (int)done;
		}
		if (r == 0) {
			break;
		}
		done += (u_long)r;
		fd_add_offset(fd, (u_long)r);
	}
	return (int)done;
}

static int file_close(struct Fd *fd) {
	return fd_close_slot(fd2num(fd));
}

static int file_stat(struct Fd *fd, struct Stat *statbuf) {
	struct FsStat fsstat;
	int backend = fd_backend(fd);
	int r;

	if (backend < 0) {
		return backend;
	}
	r = fsipc_fstat(backend, &fsstat);
	if (r < 0) {
		return r;
	}
	stat_from_fs(statbuf, &fsstat);
	return 0;
}

static int file_seek(struct Fd *fd, u_long offset) {
	fd_set_offset(fd, offset);
	return sync_kernel_offset(fd);
}

static int pipe_read(struct Fd *fd, void *buf, u_long n) {
	int backend = fd_backend(fd);
	int r;

	if (backend < 0) {
		return backend;
	}
	do {
		r = syscall_fs_read(backend, buf, n);
		if (r == -E_IPC_NOT_RECV) {
			syscall_yield();
		}
	} while (r == -E_IPC_NOT_RECV);
	return r;
}

static int pipe_write(struct Fd *fd, const void *buf, u_long n) {
	int backend = fd_backend(fd);
	int r;

	if (backend < 0) {
		return backend;
	}
	do {
		r = syscall_fs_write(backend, buf, n);
		if (r == -E_IPC_NOT_RECV) {
			syscall_yield();
		}
	} while (r == -E_IPC_NOT_RECV);
	return r;
}

static int pipe_close(struct Fd *fd) {
	int fdnum = fd2num(fd);
	int backend = fd_backend(fd);
	void *data = fd2data(fd);
	int r;

	if (fdnum < 0 || backend < 0) {
		return -E_INVAL;
	}
	for (u_long off = 0; off < FILEDATA_SIZE; off += PAGE_SIZE) {
		(void)syscall_mem_unmap(0, (char *)data + off);
	}
	if (fd->fd_ref > 1) {
		fd->fd_ref--;
		return syscall_mem_unmap(0, fd);
	}
	r = syscall_fs_close(backend);
	(void)syscall_mem_unmap(0, fd);
	return r;
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
	char full[FS_PATH_MAX];
	int backend;
	int r = resolve_path(path, full);

	if (r < 0) {
		return r;
	}
	r = fsipc_open(full, (u32)flags, &backend);
	if (r < 0) {
		return r;
	}
	return fd_install(-1, DEV_FILE, (u32)flags, backend);
}

int read(int fdnum, void *buf, u_long n) {
	struct Fd *fd;
	struct Dev *dev;
	int r;

	if ((r = fd_lookup(fdnum, &fd)) < 0 || (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
		return r;
	}
	return dev->dev_read == 0 ? -E_INVAL : dev->dev_read(fd, buf, n);
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

int read_map(int fdnum, u_long offset, void **blk) {
	struct Fd *fd;
	int backend;
	int r;
	u_long page_off = ROUNDDOWN(offset, PAGE_SIZE);
	u_long in_page = offset - page_off;
	void *dst;

	if (blk == 0) {
		return -E_INVAL;
	}
	r = fd_lookup(fdnum, &fd);
	if (r < 0) {
		return r;
	}
	if (fd->fd_dev_id != DEV_FILE) {
		return -E_INVAL;
	}
	backend = fd_backend(fd);
	if (backend < 0) {
		return backend;
	}
	if (page_off >= FILEDATA_SIZE) {
		return -E_INVAL;
	}
	dst = (char *)fd2data(fd) + page_off;
	r = fsipc_map(backend, page_off, dst, PAGE_SIZE);
	if (r < 0) {
		return r;
	}
	if ((u_long)r <= in_page) {
		*blk = dst;
		return 0;
	}
	*blk = (char *)dst + in_page;
	return r;
}

int dirty(int fdnum, u_long offset, u_long len) {
	struct Fd *fd;
	int backend;
	int r;

	r = fd_lookup(fdnum, &fd);
	if (r < 0) {
		return r;
	}
	if (fd->fd_dev_id != DEV_FILE) {
		return -E_INVAL;
	}
	backend = fd_backend(fd);
	return backend < 0 ? backend : fsipc_dirty(backend, offset, len);
}

int write(int fdnum, const void *buf, u_long n) {
	struct Fd *fd;
	struct Dev *dev;
	int r;

	r = fd_lookup(fdnum, &fd);
	if (r < 0 && (fdnum == 1 || fdnum == 2)) {
		return syscall_print_cons(buf, n) < 0 ? -E_INVAL : (int)n;
	}
	if (r < 0 || (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
		return r;
	}
	return dev->dev_write == 0 ? -E_INVAL : dev->dev_write(fd, buf, n);
}

int seek(int fdnum, u_long offset) {
	struct Fd *fd;
	struct Dev *dev;
	int r;

	if ((r = fd_lookup(fdnum, &fd)) < 0 || (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
		return r;
	}
	return dev->dev_seek == 0 ? -E_INVAL : dev->dev_seek(fd, offset);
}

int stat(const char *path, struct Stat *statbuf) {
	char full[FS_PATH_MAX];
	struct FsStat fsstat;
	int r = resolve_path(path, full);

	if (r < 0) {
		return r;
	}
	r = fsipc_stat(full, &fsstat);
	if (r < 0) {
		return r;
	}
	stat_from_fs(statbuf, &fsstat);
	return 0;
}

int fstat(int fdnum, struct Stat *statbuf) {
	struct Fd *fd;
	struct Dev *dev;
	int r;

	if ((r = fd_lookup(fdnum, &fd)) < 0 || (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
		return r;
	}
	return dev->dev_stat == 0 ? -E_INVAL : dev->dev_stat(fd, statbuf);
}

int close(int fdnum) {
	struct Fd *fd;
	struct Dev *dev;
	int r;

	if ((r = fd_lookup(fdnum, &fd)) < 0 || (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
		return r;
	}
	return dev->dev_close == 0 ? -E_INVAL : dev->dev_close(fd);
}

void close_all(void) {
	for (int fd = 0; fd < FS_MAX_FD; fd++) {
		(void)close(fd);
	}
}

int dup(int oldfd, int newfd) {
	return fd_dup(oldfd, newfd);
}

int remove(const char *path) {
	char full[FS_PATH_MAX];
	int r = resolve_path(path, full);

	return r < 0 ? r : fsipc_remove(full);
}

int rename(const char *old_path, const char *new_path) {
	char old_full[FS_PATH_MAX];
	char new_full[FS_PATH_MAX];
	int r = resolve_path(old_path, old_full);

	if (r < 0) {
		return r;
	}
	r = resolve_path(new_path, new_full);
	return r < 0 ? r : fsipc_rename(old_full, new_full);
}

int link(const char *old_path, const char *new_path) {
	char old_full[FS_PATH_MAX];
	char new_full[FS_PATH_MAX];
	int r = resolve_path(old_path, old_full);

	if (r < 0) {
		return r;
	}
	r = resolve_path(new_path, new_full);
	return r < 0 ? r : fsipc_link(old_full, new_full);
}

int symlink(const char *target, const char *link_path) {
	char target_full[FS_PATH_MAX];
	char link_full[FS_PATH_MAX];
	int r = resolve_path(target, target_full);

	if (r < 0) {
		return r;
	}
	r = resolve_path(link_path, link_full);
	return r < 0 ? r : fsipc_symlink(target_full, link_full);
}

int readlink(const char *path, char *buf, u_long len) {
	char full[FS_PATH_MAX];
	int r = resolve_path(path, full);

	return r < 0 ? r : fsipc_readlink(full, buf, len);
}

int chmod(const char *path, u32 mode) {
	char full[FS_PATH_MAX];
	int r = resolve_path(path, full);

	return r < 0 ? r : fsipc_chmod(full, mode);
}

int ftruncate(int fdnum, u_long size) {
	struct Fd *fd;
	int backend;
	int r;

	if ((r = fd_lookup(fdnum, &fd)) < 0) {
		return r;
	}
	backend = fd_backend(fd);
	return backend < 0 ? backend : fsipc_set_size(backend, size);
}

int mkdir(const char *path) {
	char full[FS_PATH_MAX];
	int r = resolve_path(path, full);

	return r < 0 ? r : fsipc_mkdir(full);
}

int sync(void) {
	return fsipc_sync();
}

int chdir(const char *path) {
	char full[FS_PATH_MAX];
	struct Stat st;
	int r = resolve_path(path, full);

	if (r < 0) {
		return r;
	}
	r = stat(full, &st);
	if (r < 0) {
		return r;
	}
	if (st.st_type != FS_TYPE_DIR) {
		return -E_INVAL;
	}
	strcpy(user_cwd, full);
	return 0;
}

int getcwd(char *buf, u_long len) {
	u_long n = strlen(user_cwd);

	if (buf == 0 || len == 0 || n + 1 > len) {
		return -E_INVAL;
	}
	strcpy(buf, user_cwd);
	return 0;
}

int listdir(const char *dir, int index, char *path, u_long path_len, struct Stat *statbuf) {
	char full[FS_PATH_MAX];
	struct FsStat fsstat;
	int r = resolve_path(dir, full);

	if (r < 0) {
		return r;
	}
	r = fsipc_list_dir(full, index, path, path_len, statbuf == 0 ? 0 : &fsstat);
	if (r > 0 && statbuf != 0) {
		stat_from_fs(statbuf, &fsstat);
	}
	return r;
}

int list(int index, char *path, u_long path_len, struct Stat *statbuf) {
	return listdir("/", index, path, path_len, statbuf);
}

int getdents(int fd, struct FsDirent *dirents, u_long len) {
	u_long usable = len - (len % sizeof(struct FsDirent));

	if (dirents == 0 || usable == 0) {
		return -E_INVAL;
	}
	return read(fd, dirents, usable);
}

int pipe(int pfd[2]) {
	int backend[2];
	int r0;
	int r1;

	r0 = syscall_pipe(backend);
	if (r0 < 0) {
		return r0;
	}
	r0 = fd_install(-1, DEV_PIPE, 0, backend[0]);
	if (r0 < 0) {
		(void)syscall_fs_close(backend[0]);
		(void)syscall_fs_close(backend[1]);
		return r0;
	}
	r1 = fd_install(-1, DEV_PIPE, 0, backend[1]);
	if (r1 < 0) {
		(void)close(r0);
		(void)syscall_fs_close(backend[1]);
		return r1;
	}
	pfd[0] = r0;
	pfd[1] = r1;
	return 0;
}

int cgetc(void) {
	return syscall_cgetc();
}

int spawn(char *prog, char **argv) {
	char full[FS_PATH_MAX];
	int r = resolve_path(prog, full);

	return r < 0 ? r : syscall_spawn(full, (long)argv);
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
