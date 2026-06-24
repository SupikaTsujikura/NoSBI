#ifndef _MOS_RISCV_USER_FD_H_
#define _MOS_RISCV_USER_FD_H_

#include <fs.h>
#include <types.h>

#include "lib.h"

#define MAXFD FS_MAX_FD
#define FDTABLE FS_USER_FDTABLE
#define FILEDATA_BASE FS_USER_FILEDATA_BASE
#define FILEDATA_SIZE FS_USER_FILEDATA_SIZE
#define INDEX2FD(i) ((struct Fd *)(FDTABLE + (u_long)(i) * PAGE_SIZE))
#define INDEX2DATA(i) ((void *)(FILEDATA_BASE + (u_long)(i) * FILEDATA_SIZE))

#define DEV_FILE 'f'
#define DEV_PIPE 'p'

struct Fd {
	u32 fd_dev_id;
	u32 fd_omode;
	int fd_openid;
	int fd_backend_fd;
	u_long fd_offset;
	u32 fd_ref;
};

struct Dev {
	u32 dev_id;
	const char *dev_name;
	int (*dev_read)(struct Fd *fd, void *buf, u_long n);
	int (*dev_write)(struct Fd *fd, const void *buf, u_long n);
	int (*dev_close)(struct Fd *fd);
	int (*dev_stat)(struct Fd *fd, struct Stat *stat);
	int (*dev_seek)(struct Fd *fd, u_long offset);
};

int fd_alloc(struct Fd **fd);
int fd_lookup(int fdnum, struct Fd **fd);
int fd_install(int fdnum, u32 dev_id, u32 omode, int backend_fd);
int fd_backend(struct Fd *fd);
u_long fd_offset(struct Fd *fd);
void fd_set_offset(struct Fd *fd, u_long offset);
void fd_add_offset(struct Fd *fd, u_long n);
void *fd2data(struct Fd *fd);
int fd2num(struct Fd *fd);
int dev_lookup(u32 dev_id, struct Dev **dev);
int fd_dup(int oldfdnum, int newfdnum);
int fd_close_slot(int fdnum);

extern struct Dev devfile;
extern struct Dev devpipe;

#endif
