#ifndef _MOS_RISCV_USER_FSREQ_H_
#define _MOS_RISCV_USER_FSREQ_H_

#include <fs.h>
#include <types.h>

#include "lib.h"

#define FSSERV_NAME "fsserv"
#define FSIPC_MAX_DATA 3500

enum {
	FSREQ_OPEN,
	FSREQ_MAP,
	FSREQ_READ,
	FSREQ_WRITE,
	FSREQ_SEEK,
	FSREQ_SET_SIZE,
	FSREQ_STAT,
	FSREQ_FSTAT,
	FSREQ_CLOSE,
	FSREQ_DIRTY,
	FSREQ_REMOVE,
	FSREQ_RENAME,
	FSREQ_MKDIR,
	FSREQ_SYNC,
	FSREQ_LIST,
	FSREQ_GETDENTS,
	FSREQ_LINK,
	FSREQ_SYMLINK,
	FSREQ_READLINK,
	FSREQ_CHMOD,
	MAX_FSREQNO,
};

struct Fsreq_open {
	char req_path[FS_PATH_MAX];
	u32 req_omode;
};

struct Fsreq_io {
	int req_fileid;
	u_long req_offset;
	u_long req_len;
	char req_buf[FSIPC_MAX_DATA];
};

struct Fsreq_seek {
	int req_fileid;
	u_long req_offset;
};

struct Fsreq_set_size {
	int req_fileid;
	u_long req_size;
};

struct Fsreq_close {
	int req_fileid;
};

struct Fsreq_dirty {
	int req_fileid;
	u_long req_offset;
	u_long req_len;
};

struct Fsreq_path {
	char req_path[FS_PATH_MAX];
};

struct Fsreq_stat {
	char req_path[FS_PATH_MAX];
	struct FsStat req_stat;
};

struct Fsreq_rename {
	char req_old_path[FS_PATH_MAX];
	char req_new_path[FS_PATH_MAX];
};

struct Fsreq_link {
	char req_old_path[FS_PATH_MAX];
	char req_new_path[FS_PATH_MAX];
};

struct Fsreq_symlink {
	char req_target[FS_PATH_MAX];
	char req_link_path[FS_PATH_MAX];
};

struct Fsreq_readlink {
	char req_path[FS_PATH_MAX];
	u_long req_len;
	char req_target[FS_PATH_MAX];
};

struct Fsreq_chmod {
	char req_path[FS_PATH_MAX];
	u32 req_mode;
};

struct Fsreq_list {
	int req_index;
	u_long req_path_len;
	char req_dir[FS_PATH_MAX];
	char req_path[FS_PATH_MAX];
	struct FsStat req_stat;
};

int fsipc_open(const char *path, u32 omode, int *fileid);
int fsipc_map(int fileid, u_long offset, void *buf, u_long len);
int fsipc_read(int fileid, void *buf, u_long len);
int fsipc_write(int fileid, const void *buf, u_long len);
int fsipc_seek(int fileid, u_long offset);
int fsipc_set_size(int fileid, u_long size);
int fsipc_stat(const char *path, struct FsStat *stat);
int fsipc_fstat(int fileid, struct FsStat *stat);
int fsipc_close(int fileid);
int fsipc_dirty(int fileid, u_long offset, u_long len);
int fsipc_remove(const char *path);
int fsipc_rename(const char *old_path, const char *new_path);
int fsipc_link(const char *old_path, const char *new_path);
int fsipc_symlink(const char *target, const char *link_path);
int fsipc_readlink(const char *path, char *buf, u_long len);
int fsipc_chmod(const char *path, u32 mode);
int fsipc_mkdir(const char *path);
int fsipc_sync(void);
int fsipc_list(int index, char *path, u_long path_len, struct FsStat *stat);
int fsipc_list_dir(const char *dir, int index, char *path, u_long path_len,
                   struct FsStat *stat);
int fsipc_getdents(int fileid, void *buf, u_long len);

#endif
