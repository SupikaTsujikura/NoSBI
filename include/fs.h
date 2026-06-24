#ifndef _MOS_RISCV_FS_H_
#define _MOS_RISCV_FS_H_

#include <types.h>

#define FS_MAX_FD 16
#define FS_MAX_OPEN_FILES 64
#define FS_PATH_MAX 128
#define FS_MEM_MAX_FILES 64
#define FS_MEM_FILE_CAPACITY 65536
#define FS_MAX_PIPES 16
#define FS_PIPE_CAPACITY 256
#define FS_USER_FDTABLE 0x1ff00000UL
#define FS_USER_FILEDATA_BASE 0x20000000UL
#define FS_USER_FILEDATA_SIZE (1024UL * 1024UL)

#define FS_OPEN_CREATE 0x1
#define FS_OPEN_TRUNC 0x2
#define FS_OPEN_APPEND 0x4

struct FsStat {
	u64 size;
	u32 type;
	u32 mode;
	u32 nlink;
	u32 uid;
	u32 gid;
};

struct FsDirent {
	u64 size;
	u32 type;
	u32 name_len;
	char name[FS_PATH_MAX];
};

#define FS_TYPE_FILE 1
#define FS_TYPE_DIR 2
#define FS_TYPE_SYMLINK 3

#define FS_MODE_IRUSR 0400
#define FS_MODE_IWUSR 0200
#define FS_MODE_IXUSR 0100
#define FS_MODE_IRGRP 0040
#define FS_MODE_IWGRP 0020
#define FS_MODE_IXGRP 0010
#define FS_MODE_IROTH 0004
#define FS_MODE_IWOTH 0002
#define FS_MODE_IXOTH 0001
#define FS_MODE_FILE_DEFAULT 0644
#define FS_MODE_DIR_DEFAULT 0755

#define FD_NONE 0
#define FD_FILE 1
#define FD_PIPE_READ 2
#define FD_PIPE_WRITE 3

void fs_init(void);
int fs_open_path(const char *path, int flags);
int fs_read_file(int fileid, size_t off, void *buf, size_t len);
int fs_write_file(int fileid, size_t off, const void *buf, size_t len);
int fs_truncate_file(int fileid, size_t size);
int fs_stat_file(int fileid, struct FsStat *stat);
int fs_stat_path(const char *path, struct FsStat *stat);
int fs_read_all(const char *path, const void **data, size_t *size);
int fs_unlink_path(const char *path);
int fs_rename_path(const char *old_path, const char *new_path);
int fs_mkdir_path(const char *path);
int fs_chmod_path(const char *path, u32 mode);
int fs_sync(void);
int fs_list(int index, char *path, size_t path_len, struct FsStat *stat);

#endif
