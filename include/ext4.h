#ifndef _MOS_RISCV_EXT4_H_
#define _MOS_RISCV_EXT4_H_

#include <types.h>

struct Ext4Stat {
	int is_dir;
	size_t size;
	int nlink;
};

struct Ext4Dirent {
	u32 inode;
	int is_dir;
	char name[64];
};

void ext4_init(void);
int ext4_available(void);
int ext4_lookup_path(const char *path, u32 *inode);
int ext4_stat(u32 inode, struct Ext4Stat *st);
long ext4_read(u32 inode, size_t *pos, void *buf, size_t count);
long ext4_getdents(u32 inode, size_t *pos, struct Ext4Dirent *dirent);
int ext4_read_all(const char *path, const void **image, size_t *size);

#endif
