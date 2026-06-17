#include <elf.h>
#include <error.h>
#include <ext4.h>
#include <fs.h>
#include <string.h>

#define FS_EXT4_FILEID_BASE 0x100000

extern char _binary_build_user_demo_elf_start[];
extern char _binary_build_user_demo_elf_end[];
extern char _binary_build_user_reader_elf_start[];
extern char _binary_build_user_reader_elf_end[];
extern char _binary_build_user_argvtest_elf_start[];
extern char _binary_build_user_argvtest_elf_end[];

struct MemFile {
	char path[FS_PATH_MAX];
	void *data;
	size_t size;
	size_t capacity;
	u32 type;
	int writable;
	int used;
};

static const char hello_text[] =
    "hello from NoSBI memfs\n"
    "this file is served through the kernel VFS path\n";
static u8 writable_storage[FS_MEM_MAX_FILES][FS_MEM_FILE_CAPACITY];

static struct MemFile memfiles[] = {
	{"/hello.txt", (void *)hello_text, sizeof(hello_text) - 1, sizeof(hello_text) - 1,
	 FS_TYPE_FILE, 0, 1},
	{"hello.txt", (void *)hello_text, sizeof(hello_text) - 1, sizeof(hello_text) - 1,
	 FS_TYPE_FILE, 0, 1},
	{"/bin/demo", NULL, 0, 0, FS_TYPE_FILE, 0, 1},
	{"bin/demo", NULL, 0, 0, FS_TYPE_FILE, 0, 1},
	{"/bin/reader", NULL, 0, 0, FS_TYPE_FILE, 0, 1},
	{"bin/reader", NULL, 0, 0, FS_TYPE_FILE, 0, 1},
	{"/bin/argvtest", NULL, 0, 0, FS_TYPE_FILE, 0, 1},
	{"bin/argvtest", NULL, 0, 0, FS_TYPE_FILE, 0, 1},
};
static struct MemFile dynfiles[FS_MEM_MAX_FILES];

void fs_init(void) {
	const void *demo = _binary_build_user_demo_elf_start;
	size_t demo_size =
	    (size_t)(_binary_build_user_demo_elf_end - _binary_build_user_demo_elf_start);
	const void *reader = _binary_build_user_reader_elf_start;
	size_t reader_size =
	    (size_t)(_binary_build_user_reader_elf_end - _binary_build_user_reader_elf_start);
	const void *argvtest = _binary_build_user_argvtest_elf_start;
	size_t argvtest_size = (size_t)(_binary_build_user_argvtest_elf_end -
	                                _binary_build_user_argvtest_elf_start);

	for (size_t i = 0; i < sizeof(memfiles) / sizeof(memfiles[0]); i++) {
		if (strcmp(memfiles[i].path, "/bin/demo") == 0 ||
		    strcmp(memfiles[i].path, "bin/demo") == 0) {
			memfiles[i].data = (void *)demo;
			memfiles[i].size = demo_size;
			memfiles[i].capacity = demo_size;
		} else if (strcmp(memfiles[i].path, "/bin/reader") == 0 ||
		           strcmp(memfiles[i].path, "bin/reader") == 0) {
			memfiles[i].data = (void *)reader;
			memfiles[i].size = reader_size;
			memfiles[i].capacity = reader_size;
		} else if (strcmp(memfiles[i].path, "/bin/argvtest") == 0 ||
		           strcmp(memfiles[i].path, "bin/argvtest") == 0) {
			memfiles[i].data = (void *)argvtest;
			memfiles[i].size = argvtest_size;
			memfiles[i].capacity = argvtest_size;
		}
	}
	ext4_init();
}

static int path_equal(const char *a, const char *b) {
	if (a == NULL || b == NULL) {
		return 0;
	}
	return strcmp(a, b) == 0;
}

static int static_file_count(void) {
	return (int)(sizeof(memfiles) / sizeof(memfiles[0]));
}

static int ext4_fileid(u32 inode) {
	return FS_EXT4_FILEID_BASE + (int)inode;
}

static int fileid_is_ext4(int fileid) {
	return fileid >= FS_EXT4_FILEID_BASE;
}

static u32 fileid_to_ext4_inode(int fileid) {
	return (u32)(fileid - FS_EXT4_FILEID_BASE);
}

static struct MemFile *file_by_id(int fileid) {
	int nstatic = static_file_count();

	if (fileid >= 0 && fileid < nstatic) {
		return &memfiles[fileid];
	}
	fileid -= nstatic;
	if (fileid >= 0 && fileid < FS_MEM_MAX_FILES && dynfiles[fileid].used) {
		return &dynfiles[fileid];
	}
	return NULL;
}

int fs_open_path(const char *path, int flags) {
	for (size_t i = 0; i < sizeof(memfiles) / sizeof(memfiles[0]); i++) {
		if (path_equal(path, memfiles[i].path)) {
			if (flags & FS_OPEN_TRUNC) {
				return -E_INVAL;
			}
			return (int)i;
		}
	}
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		if (dynfiles[i].used && path_equal(path, dynfiles[i].path)) {
			if (flags & FS_OPEN_TRUNC) {
				dynfiles[i].size = 0;
			}
			return static_file_count() + i;
		}
	}
	if (flags & FS_OPEN_CREATE) {
		for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
			if (!dynfiles[i].used) {
				strcpy(dynfiles[i].path, path);
				dynfiles[i].data = writable_storage[i];
				dynfiles[i].size = 0;
				dynfiles[i].capacity = FS_MEM_FILE_CAPACITY;
				dynfiles[i].type = FS_TYPE_FILE;
				dynfiles[i].writable = 1;
				dynfiles[i].used = 1;
				return static_file_count() + i;
			}
		}
		return -E_NO_MEM;
	}
	if (ext4_available()) {
		u32 ino;

		if (ext4_lookup_path(path, &ino) == 0) {
			struct Ext4Stat stat;

			if ((flags & (FS_OPEN_CREATE | FS_OPEN_TRUNC)) != 0) {
				return -E_INVAL;
			}
			if (ext4_stat(ino, &stat) < 0 || stat.is_dir) {
				return -E_INVAL;
			}
			return ext4_fileid(ino);
		}
	}
	return -E_INVAL;
}

int fs_read_file(int fileid, size_t off, void *buf, size_t len) {
	struct MemFile *file = file_by_id(fileid);
	size_t n;

	if (fileid_is_ext4(fileid)) {
		size_t pos = off;
		long r = ext4_read(fileid_to_ext4_inode(fileid), &pos, buf, len);

		return r < 0 ? (int)r : (int)r;
	}
	if (file == NULL || buf == NULL) {
		return -E_INVAL;
	}
	if (off >= file->size) {
		return 0;
	}
	n = file->size - off;
	if (n > len) {
		n = len;
	}
	memcpy(buf, (const u8 *)file->data + off, n);
	return (int)n;
}

int fs_write_file(int fileid, size_t off, const void *buf, size_t len) {
	struct MemFile *file = file_by_id(fileid);

	if (fileid_is_ext4(fileid)) {
		return -E_INVAL;
	}
	if (file == NULL || buf == NULL || !file->writable) {
		return -E_INVAL;
	}
	if (off > file->capacity || len > file->capacity - off) {
		return -E_NO_MEM;
	}
	memcpy((u8 *)file->data + off, buf, len);
	if (off + len > file->size) {
		file->size = off + len;
	}
	return (int)len;
}

int fs_truncate_file(int fileid, size_t size) {
	struct MemFile *file = file_by_id(fileid);

	if (fileid_is_ext4(fileid)) {
		return -E_INVAL;
	}
	if (file == NULL || !file->writable || size > file->capacity) {
		return -E_INVAL;
	}
	if (size > file->size) {
		memset((u8 *)file->data + file->size, 0, size - file->size);
	}
	file->size = size;
	return 0;
}

int fs_stat_file(int fileid, struct FsStat *stat) {
	struct MemFile *file = file_by_id(fileid);

	if (fileid_is_ext4(fileid)) {
		struct Ext4Stat est;
		int r;

		if (stat == NULL) {
			return -E_INVAL;
		}
		r = ext4_stat(fileid_to_ext4_inode(fileid), &est);
		if (r < 0) {
			return r;
		}
		stat->size = est.size;
		stat->type = est.is_dir ? FS_TYPE_DIR : FS_TYPE_FILE;
		stat->reserved = 0;
		return 0;
	}
	if (fileid < 0 || file == NULL || stat == NULL) {
		return fileid < 0 ? fileid : -E_INVAL;
	}
	stat->size = file->size;
	stat->type = file->type;
	stat->reserved = 0;
	return 0;
}

int fs_stat_path(const char *path, struct FsStat *stat) {
	int fileid = fs_open_path(path, 0);

	return fs_stat_file(fileid, stat);
}

int fs_read_all(const char *path, const void **data, size_t *size) {
	int fileid = fs_open_path(path, 0);
	struct MemFile *file = file_by_id(fileid);

	if (fileid_is_ext4(fileid)) {
		return ext4_read_all(path, data, size);
	}
	if (fileid < 0 || file == NULL || data == NULL || size == NULL) {
		return fileid < 0 ? fileid : -E_INVAL;
	}
	*data = file->data;
	*size = file->size;
	return 0;
}

int fs_unlink_path(const char *path) {
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		if (dynfiles[i].used && path_equal(path, dynfiles[i].path)) {
			dynfiles[i].used = 0;
			dynfiles[i].path[0] = '\0';
			dynfiles[i].data = NULL;
			dynfiles[i].size = 0;
			dynfiles[i].capacity = 0;
			dynfiles[i].writable = 0;
			return 0;
		}
	}
	return -E_INVAL;
}

int fs_rename_path(const char *old_path, const char *new_path) {
	if (fs_open_path(new_path, 0) >= 0) {
		return -E_INVAL;
	}
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		if (dynfiles[i].used && path_equal(old_path, dynfiles[i].path)) {
			strcpy(dynfiles[i].path, new_path);
			return 0;
		}
	}
	return -E_INVAL;
}

int fs_list(int index, char *path, size_t path_len, struct FsStat *stat) {
	struct MemFile *file = NULL;
	size_t len;
	int next_index = static_file_count();

	if (index < 0 || path == NULL || path_len == 0) {
		return -E_INVAL;
	}
	if (index < static_file_count()) {
		file = &memfiles[index];
	} else {
		int seen = static_file_count();

		for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
			if (!dynfiles[i].used) {
				continue;
			}
			if (seen == index) {
				file = &dynfiles[i];
				break;
			}
			seen++;
		}
		next_index = seen;
	}
	if (file == NULL || !file->used) {
		if (ext4_available()) {
			size_t pos = 0;
			struct Ext4Dirent de;
			int wanted = index - next_index;
			int seen = 0;

			while (ext4_getdents(2, &pos, &de) > 0) {
				if (seen++ != wanted) {
					continue;
				}
				if (strlen(de.name) + 2 > path_len) {
					return -E_INVAL;
				}
				path[0] = '/';
				strcpy(path + 1, de.name);
				if (stat != NULL) {
					struct Ext4Stat est;

					if (ext4_stat(de.inode, &est) < 0) {
						return -E_INVAL;
					}
					stat->size = est.size;
					stat->type = est.is_dir ? FS_TYPE_DIR : FS_TYPE_FILE;
					stat->reserved = 0;
				}
				return 1;
			}
		}
		return 0;
	}
	len = strlen(file->path);
	if (len + 1 > path_len) {
		return -E_INVAL;
	}
	strcpy(path, file->path);
	if (stat != NULL) {
		stat->size = file->size;
		stat->type = file->type;
		stat->reserved = 0;
	}
	return 1;
}
