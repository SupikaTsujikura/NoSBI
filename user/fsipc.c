#include <arch/vm.h>
#include <env.h>
#include <error.h>
#include <string.h>

#include "fsreq.h"

union FsipcPage {
	struct Fsreq_open open;
	struct Fsreq_io io;
	struct Fsreq_seek seek;
	struct Fsreq_set_size set_size;
	struct Fsreq_close close;
	struct Fsreq_dirty dirty;
	struct Fsreq_path path;
	struct Fsreq_stat stat;
	struct Fsreq_rename rename;
	struct Fsreq_link link;
	struct Fsreq_symlink symlink;
	struct Fsreq_readlink readlink;
	struct Fsreq_chmod chmod;
	struct Fsreq_list list;
	char raw[PAGE_SIZE];
};

static union FsipcPage fsipcbuf __attribute__((aligned(PAGE_SIZE)));
static int cached_fsserv;
static int fsserv_missing;

static int check_path(const char *path) {
	if (path == 0 || path[0] == '\0' || strlen(path) >= FS_PATH_MAX) {
		return -E_INVAL;
	}
	return 0;
}

static int fsserv_envid(void) {
	int envid;

	if (fsserv_missing) {
		return -E_BAD_ENV;
	}
	if (cached_fsserv > 0 && syscall_env_status((u_long)cached_fsserv) != ENV_FREE) {
		return cached_fsserv;
	}
	envid = syscall_env_find(FSSERV_NAME);
	if (envid < 0) {
		fsserv_missing = 1;
		return envid;
	}
	cached_fsserv = envid;
	return envid;
}

static int fsipc_call_recv(u_long type, void *dstva) {
	int fsenv = fsserv_envid();
	u_long val = 0;
	u_long from = 0;
	u_long perm = 0;
	int r;

	if (fsenv < 0) {
		return fsenv;
	}
	r = ipc_send((u_long)fsenv, type, &fsipcbuf, PTE_D);
	if (r < 0) {
		return r;
	}
	if (dstva != 0) {
		(void)syscall_mem_unmap(0, dstva);
	}
	r = ipc_recv(&from, &val, dstva, &perm);
	if (r < 0) {
		return r;
	}
	(void)from;
	(void)perm;
	return (int)val;
}

static int fsipc_call(u_long type) {
	return fsipc_call_recv(type, 0);
}

int fsipc_open(const char *path, u32 omode, int *fileid) {
	struct Fsreq_open *req = &fsipcbuf.open;
	int r;

	if (fileid == 0) {
		return -E_INVAL;
	}
	if ((r = check_path(path)) < 0) {
		return r;
	}
	strcpy(req->req_path, path);
	req->req_omode = omode;
	r = fsipc_call(FSREQ_OPEN);
	if (r == -E_BAD_ENV) {
		r = syscall_fs_open_flags(req->req_path, (int)req->req_omode);
	}
	if (r < 0) {
		return r;
	}
	*fileid = r;
	return 0;
}

int fsipc_read(int fileid, void *buf, u_long len) {
	struct Fsreq_io *req = &fsipcbuf.io;
	int r;

	if (len > FSIPC_MAX_DATA) {
		len = FSIPC_MAX_DATA;
	}
	req->req_fileid = fileid;
	req->req_len = len;
	r = fsipc_call(FSREQ_READ);
	if (r == -E_BAD_ENV) {
		do {
			r = syscall_fs_read(req->req_fileid, buf, req->req_len);
			if (r == -E_IPC_NOT_RECV) {
				syscall_yield();
			}
		} while (r == -E_IPC_NOT_RECV);
		return r;
	}
	if (r > 0) {
		memcpy(buf, req->req_buf, (u_long)r);
	}
	return r;
}

int fsipc_map(int fileid, u_long offset, void *buf, u_long len) {
	struct Fsreq_io *req = &fsipcbuf.io;
	int r;

	if (buf == 0) {
		return -E_INVAL;
	}
	if (len > FSIPC_MAX_DATA) {
		len = FSIPC_MAX_DATA;
	}
	req->req_fileid = fileid;
	req->req_offset = offset;
	req->req_len = len;
	r = fsipc_call_recv(FSREQ_MAP, buf);
	if (r == -E_BAD_ENV) {
		(void)syscall_fs_seek(fileid, offset);
		r = syscall_fs_read(fileid, buf, len);
		return r;
	}
	return r;
}

int fsipc_write(int fileid, const void *buf, u_long len) {
	struct Fsreq_io *req = &fsipcbuf.io;
	int r;

	if (len > FSIPC_MAX_DATA) {
		len = FSIPC_MAX_DATA;
	}
	req->req_fileid = fileid;
	req->req_len = len;
	memcpy(req->req_buf, buf, len);
	r = fsipc_call(FSREQ_WRITE);
	if (r == -E_BAD_ENV) {
		do {
			r = syscall_fs_write(req->req_fileid, buf, req->req_len);
			if (r == -E_IPC_NOT_RECV) {
				syscall_yield();
			}
		} while (r == -E_IPC_NOT_RECV);
	}
	return r;
}

int fsipc_seek(int fileid, u_long offset) {
	struct Fsreq_seek *req = &fsipcbuf.seek;
	int r;

	req->req_fileid = fileid;
	req->req_offset = offset;
	r = fsipc_call(FSREQ_SEEK);
	return r == -E_BAD_ENV ? syscall_fs_seek(req->req_fileid, req->req_offset) : r;
}

int fsipc_set_size(int fileid, u_long size) {
	struct Fsreq_set_size *req = &fsipcbuf.set_size;
	int r;

	req->req_fileid = fileid;
	req->req_size = size;
	r = fsipc_call(FSREQ_SET_SIZE);
	return r == -E_BAD_ENV ? syscall_fs_truncate(req->req_fileid, req->req_size) : r;
}

int fsipc_stat(const char *path, struct FsStat *stat) {
	struct Fsreq_stat *req = &fsipcbuf.stat;
	int r;

	if ((r = check_path(path)) < 0) {
		return r;
	}
	strcpy(req->req_path, path);
	r = fsipc_call(FSREQ_STAT);
	if (r == -E_BAD_ENV) {
		return syscall_fs_stat(req->req_path, stat);
	}
	if (r == 0 && stat != 0) {
		memcpy(stat, &req->req_stat, sizeof(*stat));
	}
	return r;
}

int fsipc_fstat(int fileid, struct FsStat *stat) {
	struct Fsreq_io *req = &fsipcbuf.io;
	int r;

	if (stat == 0) {
		return -E_INVAL;
	}
	req->req_fileid = fileid;
	r = fsipc_call(FSREQ_FSTAT);
	if (r == -E_BAD_ENV) {
		return syscall_fs_fstat(fileid, stat);
	}
	if (r == 0) {
		memcpy(stat, req->req_buf, sizeof(*stat));
	}
	return r;
}

int fsipc_close(int fileid) {
	struct Fsreq_close *req = &fsipcbuf.close;
	int r;

	req->req_fileid = fileid;
	r = fsipc_call(FSREQ_CLOSE);
	return r == -E_BAD_ENV ? syscall_fs_close(req->req_fileid) : r;
}

int fsipc_dirty(int fileid, u_long offset, u_long len) {
	struct Fsreq_dirty *req = &fsipcbuf.dirty;
	int r;

	req->req_fileid = fileid;
	req->req_offset = offset;
	req->req_len = len;
	r = fsipc_call(FSREQ_DIRTY);
	return r == -E_BAD_ENV ? 0 : r;
}

int fsipc_remove(const char *path) {
	struct Fsreq_path *req = &fsipcbuf.path;
	int r;

	if ((r = check_path(path)) < 0) {
		return r;
	}
	strcpy(req->req_path, path);
	r = fsipc_call(FSREQ_REMOVE);
	return r == -E_BAD_ENV ? syscall_fs_unlink(req->req_path) : r;
}

int fsipc_rename(const char *old_path, const char *new_path) {
	struct Fsreq_rename *req = &fsipcbuf.rename;
	int r;

	if ((r = check_path(old_path)) < 0) {
		return r;
	}
	if ((r = check_path(new_path)) < 0) {
		return r;
	}
	strcpy(req->req_old_path, old_path);
	strcpy(req->req_new_path, new_path);
	r = fsipc_call(FSREQ_RENAME);
	return r == -E_BAD_ENV ? syscall_fs_rename(req->req_old_path, req->req_new_path) : r;
}

int fsipc_link(const char *old_path, const char *new_path) {
	struct Fsreq_link *req = &fsipcbuf.link;
	int r;

	if ((r = check_path(old_path)) < 0 || (r = check_path(new_path)) < 0) {
		return r;
	}
	strcpy(req->req_old_path, old_path);
	strcpy(req->req_new_path, new_path);
	return fsipc_call(FSREQ_LINK);
}

int fsipc_symlink(const char *target, const char *link_path) {
	struct Fsreq_symlink *req = &fsipcbuf.symlink;
	int r;

	if ((r = check_path(target)) < 0 || (r = check_path(link_path)) < 0) {
		return r;
	}
	strcpy(req->req_target, target);
	strcpy(req->req_link_path, link_path);
	return fsipc_call(FSREQ_SYMLINK);
}

int fsipc_readlink(const char *path, char *buf, u_long len) {
	struct Fsreq_readlink *req = &fsipcbuf.readlink;
	int r;

	if ((r = check_path(path)) < 0 || buf == 0 || len == 0) {
		return r < 0 ? r : -E_INVAL;
	}
	strcpy(req->req_path, path);
	req->req_len = len > FS_PATH_MAX ? FS_PATH_MAX : len;
	r = fsipc_call(FSREQ_READLINK);
	if (r >= 0) {
		memcpy(buf, req->req_target, (u_long)r);
		if ((u_long)r < len) {
			buf[r] = '\0';
		}
	}
	return r;
}

int fsipc_chmod(const char *path, u32 mode) {
	struct Fsreq_chmod *req = &fsipcbuf.chmod;
	int r;

	if ((r = check_path(path)) < 0) {
		return r;
	}
	strcpy(req->req_path, path);
	req->req_mode = mode & 0777;
	r = fsipc_call(FSREQ_CHMOD);
	return r == -E_BAD_ENV ? syscall_fs_chmod(path, mode) : r;
}

int fsipc_mkdir(const char *path) {
	struct Fsreq_path *req = &fsipcbuf.path;
	int r;

	if ((r = check_path(path)) < 0) {
		return r;
	}
	strcpy(req->req_path, path);
	r = fsipc_call(FSREQ_MKDIR);
	return r == -E_BAD_ENV ? syscall_fs_mkdir(req->req_path) : r;
}

int fsipc_sync(void) {
	int r = fsipc_call(FSREQ_SYNC);

	return r == -E_BAD_ENV ? syscall_fs_sync() : r;
}

int fsipc_list_dir(const char *dir, int index, char *path, u_long path_len,
                   struct FsStat *stat) {
	struct Fsreq_list *req = &fsipcbuf.list;
	int r;

	if (dir == 0 || index < 0 || path == 0 || path_len == 0) {
		return -E_INVAL;
	}
	if ((r = check_path(dir)) < 0) {
		return r;
	}
	strcpy(req->req_dir, dir);
	req->req_index = index;
	req->req_path_len = path_len > FS_PATH_MAX ? FS_PATH_MAX : path_len;
	r = fsipc_call(FSREQ_LIST);
	if (r == -E_BAD_ENV) {
		return syscall_fs_list(index, path, path_len, stat);
	}
	if (r > 0) {
		if (strlen(req->req_path) + 1 > path_len) {
			return -E_INVAL;
		}
		strcpy(path, req->req_path);
		if (stat != 0) {
			memcpy(stat, &req->req_stat, sizeof(*stat));
		}
	}
	return r;
}

int fsipc_list(int index, char *path, u_long path_len, struct FsStat *stat) {
	return fsipc_list_dir("/", index, path, path_len, stat);
}

int fsipc_getdents(int fileid, void *buf, u_long len) {
	struct Fsreq_io *req = &fsipcbuf.io;
	int r;

	if (buf == 0 || len == 0) {
		return -E_INVAL;
	}
	if (len > FSIPC_MAX_DATA) {
		len = FSIPC_MAX_DATA;
	}
	req->req_fileid = fileid;
	req->req_len = len;
	r = fsipc_call(FSREQ_GETDENTS);
	if (r > 0) {
		memcpy(buf, req->req_buf, (u_long)r);
	}
	return r;
}
