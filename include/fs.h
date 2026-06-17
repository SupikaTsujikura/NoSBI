#ifndef _MOS_RISCV_FS_H_
#define _MOS_RISCV_FS_H_

#include <types.h>

#define FS_MAX_FD 16
#define FS_PATH_MAX 64
#define FS_MEM_MAX_FILES 16
#define FS_MEM_FILE_CAPACITY 4096
#define FS_MAX_PIPES 16
#define FS_PIPE_CAPACITY 256

#define FS_OPEN_CREATE 0x1
#define FS_OPEN_TRUNC 0x2
#define FS_OPEN_APPEND 0x4

struct FsStat {
	u64 size;
	u32 type;
	u32 reserved;
};

#define FS_TYPE_FILE 1
#define FS_TYPE_DIR 2

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
int fs_list(int index, char *path, size_t path_len, struct FsStat *stat);

#endif
