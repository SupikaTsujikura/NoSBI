#include <error.h>
#include <string.h>

#include "fd.h"
#include "fsreq.h"

static struct Dev *devtab[] = {
	&devfile,
	&devpipe,
	0,
};

static int fd_alloc_from(int start, struct Fd **fd) {
	for (int i = start; i < MAXFD; i++) {
		struct UserPageInfo info;

		if (syscall_page_info(INDEX2FD(i), &info) == 0 && !info.present) {
			*fd = INDEX2FD(i);
			return i;
		}
	}
	return -E_NO_MEM;
}

int fd_alloc(struct Fd **fd) {
	return fd_alloc_from(3, fd);
}

int fd_lookup(int fdnum, struct Fd **fd) {
	struct UserPageInfo info;

	if (fdnum < 0 || fdnum >= MAXFD ||
	    syscall_page_info(INDEX2FD(fdnum), &info) < 0 || !info.present) {
		return -E_INVAL;
	}
	*fd = INDEX2FD(fdnum);
	return 0;
}

int fd_install(int fdnum, u32 dev_id, u32 omode, int backend_fd) {
	struct Fd *fd;
	int r;

	if (fdnum < 0) {
		fdnum = fd_alloc(&fd);
		if (fdnum < 0) {
			return fdnum;
		}
	} else {
		if (fdnum >= MAXFD) {
			return -E_INVAL;
		}
		if (fd_lookup(fdnum, &fd) == 0) {
			(void)fd_close_slot(fdnum);
		}
		fd = INDEX2FD(fdnum);
	}
	if ((r = syscall_mem_alloc(0, fd, PTE_D | PTE_LIBRARY)) < 0) {
		return r;
	}
	memset(fd, 0, sizeof(*fd));
	fd->fd_dev_id = dev_id;
	fd->fd_omode = omode;
	fd->fd_openid = fdnum;
	fd->fd_backend_fd = backend_fd;
	fd->fd_offset = 0;
	fd->fd_ref = 1;
	return fdnum;
}

int fd_backend(struct Fd *fd) {
	return fd == 0 ? -E_INVAL : fd->fd_backend_fd;
}

u_long fd_offset(struct Fd *fd) {
	return fd == 0 ? 0 : fd->fd_offset;
}

void fd_set_offset(struct Fd *fd, u_long offset) {
	if (fd != 0) {
		fd->fd_offset = offset;
	}
}

void fd_add_offset(struct Fd *fd, u_long n) {
	if (fd != 0) {
		fd->fd_offset += n;
	}
}

void *fd2data(struct Fd *fd) {
	int fdnum = fd2num(fd);

	if (fdnum < 0) {
		return 0;
	}
	return (void *)(FILEDATA_BASE + (u_long)fdnum * FILEDATA_SIZE);
}

int fd2num(struct Fd *fd) {
	u_long va = (u_long)fd;

	if (va < FDTABLE || va >= FDTABLE + MAXFD * PAGE_SIZE ||
	    ((va - FDTABLE) & (PAGE_SIZE - 1)) != 0) {
		return -E_INVAL;
	}
	return (int)((va - FDTABLE) / PAGE_SIZE);
}

int dev_lookup(u32 dev_id, struct Dev **dev) {
	for (int i = 0; devtab[i] != 0; i++) {
		if (devtab[i]->dev_id == dev_id) {
			*dev = devtab[i];
			return 0;
		}
	}
	*dev = 0;
	return -E_INVAL;
}

int fd_dup(int oldfdnum, int newfdnum) {
	struct Fd *oldfd;
	struct Fd *newfd;
	int r;

	if (newfdnum < 0 || newfdnum >= MAXFD) {
		return -E_INVAL;
	}
	if ((r = fd_lookup(oldfdnum, &oldfd)) < 0) {
		return r;
	}
	if (oldfdnum == newfdnum) {
		return newfdnum;
	}
	if (fd_lookup(newfdnum, &newfd) == 0) {
		(void)fd_close_slot(newfdnum);
	}
	newfd = INDEX2FD(newfdnum);
	oldfd->fd_ref++;
	if ((r = syscall_mem_map(0, oldfd, 0, newfd, PTE_D | PTE_LIBRARY)) < 0) {
		oldfd->fd_ref--;
		return r;
	}
	return newfdnum;
}

int fd_close_slot(int fdnum) {
	struct Fd *fd;
	int backend;
	int r;
	void *data;

	if ((r = fd_lookup(fdnum, &fd)) < 0) {
		return r;
	}
	backend = fd->fd_backend_fd;
	data = fd2data(fd);
	for (u_long off = 0; off < FILEDATA_SIZE; off += PAGE_SIZE) {
		(void)syscall_mem_unmap(0, (char *)data + off);
	}
	if (fd->fd_ref > 1) {
		fd->fd_ref--;
		return syscall_mem_unmap(0, fd);
	}
	memset(fd, 0, sizeof(*fd));
	(void)syscall_mem_unmap(0, fd);
	return fsipc_close(backend);
}
