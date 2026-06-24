#include <elf.h>
#include <error.h>
#include <ext4.h>
#include <fs.h>
#include <string.h>

#define FS_EXT4_FILEID_BASE 0x100000

extern char _binary_build_user_demo_elf_start[];
extern char _binary_build_user_demo_elf_end[];
extern char _binary_build_user_init_elf_start[];
extern char _binary_build_user_init_elf_end[];
extern char _binary_build_user_reader_elf_start[];
extern char _binary_build_user_reader_elf_end[];
extern char _binary_build_user_argvtest_elf_start[];
extern char _binary_build_user_argvtest_elf_end[];
extern char _binary_build_user_writeback_elf_start[];
extern char _binary_build_user_writeback_elf_end[];
extern char _binary_build_user_fsserv_elf_start[];
extern char _binary_build_user_fsserv_elf_end[];

struct MemFile {
	char path[FS_PATH_MAX];
	void *data;
	size_t size;
	size_t capacity;
	u32 type;
	u32 mode;
	u32 nlink;
	u32 uid;
	u32 gid;
	int writable;
	int used;
	size_t origin_size;
	u32 origin_inode;
	int dirty;
};

static const char hello_text[] =
    "hello from NoSBI memfs\n"
    "this file is served through the kernel VFS path\n";
static u8 writable_storage[FS_MEM_MAX_FILES][FS_MEM_FILE_CAPACITY];
static char hidden_paths[FS_MEM_MAX_FILES][FS_PATH_MAX];

static struct MemFile memfiles[] = {
	{.path = "/", .type = FS_TYPE_DIR, .used = 1},
	{.path = "/bin", .type = FS_TYPE_DIR, .used = 1},
	{.path = "/tmp", .type = FS_TYPE_DIR, .writable = 1, .used = 1},
	{.path = "/bin/init", .type = FS_TYPE_FILE, .used = 1},
	{.path = "/hello.txt", .data = (void *)hello_text,
	 .size = sizeof(hello_text) - 1, .capacity = sizeof(hello_text) - 1,
	 .type = FS_TYPE_FILE, .used = 1},
	{.path = "hello.txt", .data = (void *)hello_text,
	 .size = sizeof(hello_text) - 1, .capacity = sizeof(hello_text) - 1,
	 .type = FS_TYPE_FILE, .used = 1},
	{.path = "/bin/demo", .type = FS_TYPE_FILE, .used = 1},
	{.path = "bin/demo", .type = FS_TYPE_FILE, .used = 1},
	{.path = "/bin/reader", .type = FS_TYPE_FILE, .used = 1},
	{.path = "bin/reader", .type = FS_TYPE_FILE, .used = 1},
	{.path = "/bin/argvtest", .type = FS_TYPE_FILE, .used = 1},
	{.path = "bin/argvtest", .type = FS_TYPE_FILE, .used = 1},
	{.path = "/bin/writeback", .type = FS_TYPE_FILE, .used = 1},
	{.path = "bin/writeback", .type = FS_TYPE_FILE, .used = 1},
	{.path = "/bin/fsserv", .type = FS_TYPE_FILE, .used = 1},
	{.path = "bin/fsserv", .type = FS_TYPE_FILE, .used = 1},
};
static struct MemFile dynfiles[FS_MEM_MAX_FILES];

void fs_init(void) {
	const void *demo = _binary_build_user_demo_elf_start;
	size_t demo_size =
	    (size_t)(_binary_build_user_demo_elf_end - _binary_build_user_demo_elf_start);
	const void *init = _binary_build_user_init_elf_start;
	size_t init_size =
	    (size_t)(_binary_build_user_init_elf_end - _binary_build_user_init_elf_start);
	const void *reader = _binary_build_user_reader_elf_start;
	size_t reader_size =
	    (size_t)(_binary_build_user_reader_elf_end - _binary_build_user_reader_elf_start);
	const void *argvtest = _binary_build_user_argvtest_elf_start;
	size_t argvtest_size = (size_t)(_binary_build_user_argvtest_elf_end -
	                                _binary_build_user_argvtest_elf_start);
	const void *writeback = _binary_build_user_writeback_elf_start;
	size_t writeback_size = (size_t)(_binary_build_user_writeback_elf_end -
	                                 _binary_build_user_writeback_elf_start);
	const void *fsserv = _binary_build_user_fsserv_elf_start;
	size_t fsserv_size = (size_t)(_binary_build_user_fsserv_elf_end -
	                              _binary_build_user_fsserv_elf_start);

	for (size_t i = 0; i < sizeof(memfiles) / sizeof(memfiles[0]); i++) {
		if (memfiles[i].mode == 0) {
			memfiles[i].mode = memfiles[i].type == FS_TYPE_DIR ?
			                   FS_MODE_DIR_DEFAULT : FS_MODE_FILE_DEFAULT;
		}
		if (memfiles[i].nlink == 0) {
			memfiles[i].nlink = 1;
		}
		if (strcmp(memfiles[i].path, "/bin/init") == 0) {
			memfiles[i].data = (void *)init;
			memfiles[i].size = init_size;
			memfiles[i].capacity = init_size;
		} else if (strcmp(memfiles[i].path, "/bin/demo") == 0 ||
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
		} else if (strcmp(memfiles[i].path, "/bin/writeback") == 0 ||
		           strcmp(memfiles[i].path, "bin/writeback") == 0) {
			memfiles[i].data = (void *)writeback;
			memfiles[i].size = writeback_size;
			memfiles[i].capacity = writeback_size;
		} else if (strcmp(memfiles[i].path, "/bin/fsserv") == 0 ||
		           strcmp(memfiles[i].path, "bin/fsserv") == 0) {
			memfiles[i].data = (void *)fsserv;
			memfiles[i].size = fsserv_size;
			memfiles[i].capacity = fsserv_size;
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

static int path_hidden(const char *path) {
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		if (hidden_paths[i][0] != '\0' && path_equal(path, hidden_paths[i])) {
			return 1;
		}
	}
	return 0;
}

static int path_has_prefix_dir(const char *path, const char *prefix) {
	size_t len = strlen(prefix);

	for (size_t i = 0; i < len; i++) {
		if (path[i] != prefix[i]) {
			return 0;
		}
	}
	return path[len] == '/';
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

static int normalize_path(const char *path, char out[FS_PATH_MAX]) {
	size_t oi = 0;
	int last_slash = 0;

	if (path == NULL || path[0] == '\0') {
		return -E_INVAL;
	}
	out[oi++] = '/';
	if (path[0] == '/') {
		path++;
	}
	while (*path != '\0') {
		if (*path == '/') {
			if (!last_slash && oi > 1) {
				if (oi + 1 >= FS_PATH_MAX) {
					return -E_INVAL;
				}
				out[oi++] = '/';
			}
			last_slash = 1;
			path++;
			continue;
		}
		if (oi + 1 >= FS_PATH_MAX) {
			return -E_INVAL;
		}
		out[oi++] = *path++;
		last_slash = 0;
	}
	if (oi > 1 && out[oi - 1] == '/') {
		oi--;
	}
	out[oi] = '\0';
	return 0;
}

static int path_parent(const char *path, char parent[FS_PATH_MAX]) {
	size_t len = strlen(path);

	if (len == 0 || strcmp(path, "/") == 0) {
		return -E_INVAL;
	}
	while (len > 1 && path[len - 1] != '/') {
		len--;
	}
	if (len <= 1) {
		strcpy(parent, "/");
	} else {
		size_t n = len - 1;

		if (n >= FS_PATH_MAX) {
			return -E_INVAL;
		}
		memcpy(parent, path, n);
		parent[n] = '\0';
	}
	return 0;
}

static int mem_lookup_path(const char *path) {
	for (size_t i = 0; i < sizeof(memfiles) / sizeof(memfiles[0]); i++) {
		if (memfiles[i].used && path_equal(path, memfiles[i].path)) {
			return (int)i;
		}
	}
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		if (dynfiles[i].used && path_equal(path, dynfiles[i].path)) {
			return static_file_count() + i;
		}
	}
	return -E_INVAL;
}

static int path_is_dir(const char *path) {
	int id = mem_lookup_path(path);

	if (id >= 0) {
		struct MemFile *file = file_by_id(id);

		return file != NULL && file->type == FS_TYPE_DIR;
	}
	if (path_hidden(path)) {
		return 0;
	}
	if (ext4_available()) {
		u32 ino;
		struct Ext4Stat st;

		if (ext4_lookup_path(path, &ino) == 0 && ext4_stat(ino, &st) == 0) {
			return st.is_dir;
		}
	}
	return 0;
}

static int parent_dir_exists(const char *path) {
	char parent[FS_PATH_MAX];

	if (path_parent(path, parent) < 0) {
		return 0;
	}
	return path_is_dir(parent);
}

static int dyn_alloc_path(const char *path, u32 type, int writable) {
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		if (!dynfiles[i].used) {
			strcpy(dynfiles[i].path, path);
			dynfiles[i].data = type == FS_TYPE_FILE ? writable_storage[i] : NULL;
			dynfiles[i].size = 0;
			dynfiles[i].capacity = type == FS_TYPE_FILE ? FS_MEM_FILE_CAPACITY : 0;
			dynfiles[i].origin_size = 0;
			dynfiles[i].origin_inode = 0;
			dynfiles[i].type = type;
			dynfiles[i].mode = type == FS_TYPE_DIR ?
			                   FS_MODE_DIR_DEFAULT : FS_MODE_FILE_DEFAULT;
			dynfiles[i].nlink = 1;
			dynfiles[i].uid = 0;
			dynfiles[i].gid = 0;
			dynfiles[i].writable = writable;
			dynfiles[i].used = 1;
			dynfiles[i].dirty = 0;
			return static_file_count() + i;
		}
	}
	return -E_NO_MEM;
}

static int create_ext4_overlay(const char *path, u32 ino, int truncate) {
	struct Ext4Stat stat;
	int fileid;
	struct MemFile *file;
	size_t pos = 0;
	int n;
	int r;

	r = ext4_stat(ino, &stat);
	if (r < 0) {
		r = ext4_stat(ino, &stat);
	}
	if (r < 0) {
		return r;
	}
	if (stat.is_dir) {
		return -E_INVAL;
	}
	if (!truncate && stat.size > FS_MEM_FILE_CAPACITY) {
		return -E_NO_MEM;
	}
	fileid = dyn_alloc_path(path, FS_TYPE_FILE, 1);
	if (fileid < 0) {
		return fileid;
	}
	file = file_by_id(fileid);
	if (file == NULL) {
		return -E_INVAL;
	}
	if (truncate) {
		file->origin_inode = ino;
		file->origin_size = stat.size;
		return fileid;
	}
	while (pos < stat.size) {
		size_t chunk = stat.size - pos;

		if (chunk > 512) {
			chunk = 512;
		}
		n = ext4_read(ino, &pos, (u8 *)file->data + file->size, chunk);
		if (n < 0) {
			dynfiles[fileid - static_file_count()].used = 0;
			return n;
		}
		if (n == 0) {
			break;
		}
		file->size += (size_t)n;
	}
	file->origin_inode = ino;
	file->origin_size = stat.size;
	return fileid;
}

int fs_open_path(const char *path, int flags) {
	char norm[FS_PATH_MAX];
	int id;

	try(normalize_path(path, norm));
	id = mem_lookup_path(norm);
	if (id >= 0) {
		struct MemFile *file = file_by_id(id);

		if (file != NULL) {
			if (flags & FS_OPEN_TRUNC) {
				if (!file->writable || file->type != FS_TYPE_FILE) {
					return -E_INVAL;
				}
				file->size = 0;
			}
			if (file->type == FS_TYPE_DIR) {
				return -E_INVAL;
			}
			return id;
		}
	}
	if (path_hidden(norm)) {
		return -E_INVAL;
	}
	if (ext4_available()) {
		u32 ino;

		if (ext4_lookup_path(norm, &ino) == 0) {
			if ((flags & (FS_OPEN_CREATE | FS_OPEN_TRUNC | FS_OPEN_APPEND)) != 0) {
				return create_ext4_overlay(norm, ino, (flags & FS_OPEN_TRUNC) != 0);
			}
			return ext4_fileid(ino);
		}
	}
	if (flags & FS_OPEN_CREATE) {
		if (!parent_dir_exists(norm)) {
			return -E_INVAL;
		}
		if (ext4_available()) {
			u32 ino;

			if (ext4_create(norm, FS_MODE_FILE_DEFAULT, &ino) == 0) {
				return ext4_fileid(ino);
			}
		}
		return dyn_alloc_path(norm, FS_TYPE_FILE, 1);
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
		size_t pos = off;
		long r = ext4_write(fileid_to_ext4_inode(fileid), &pos, buf, len);

		return r < 0 ? (int)r : (int)r;
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
	file->dirty = 1;
	return (int)len;
}

int fs_truncate_file(int fileid, size_t size) {
	struct MemFile *file = file_by_id(fileid);

	if (fileid_is_ext4(fileid)) {
		return ext4_truncate(fileid_to_ext4_inode(fileid), size);
	}
	if (file == NULL || !file->writable || size > file->capacity) {
		return -E_INVAL;
	}
	if (size > file->size) {
		memset((u8 *)file->data + file->size, 0, size - file->size);
	}
	file->size = size;
	file->dirty = 1;
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
		stat->mode = est.mode;
		stat->nlink = est.nlink > 0 ? (u32)est.nlink : 1;
		stat->uid = 0;
		stat->gid = 0;
		return 0;
	}
	if (fileid < 0 || file == NULL || stat == NULL) {
		return fileid < 0 ? fileid : -E_INVAL;
	}
	stat->size = file->size;
	stat->type = file->type;
	stat->mode = file->mode;
	stat->nlink = file->nlink;
	stat->uid = file->uid;
	stat->gid = file->gid;
	return 0;
}

int fs_stat_path(const char *path, struct FsStat *stat) {
	char norm[FS_PATH_MAX];
	int fileid;

	try(normalize_path(path, norm));
	fileid = mem_lookup_path(norm);
	if (fileid < 0 && ext4_available()) {
		u32 ino;

		if (!path_hidden(norm) && ext4_lookup_path(norm, &ino) == 0) {
			fileid = ext4_fileid(ino);
		}
	}

	return fs_stat_file(fileid, stat);
}

int fs_read_all(const char *path, const void **data, size_t *size) {
	char norm[FS_PATH_MAX];
	int fileid;
	struct MemFile *file;

	try(normalize_path(path, norm));
	fileid = fs_open_path(norm, 0);
	file = file_by_id(fileid);

	if (fileid_is_ext4(fileid)) {
		return ext4_read_all(norm, data, size);
	}
	if (fileid < 0 || file == NULL || data == NULL || size == NULL) {
		return fileid < 0 ? fileid : -E_INVAL;
	}
	*data = file->data;
	*size = file->size;
	return 0;
}

int fs_unlink_path(const char *path) {
	char norm[FS_PATH_MAX];

	try(normalize_path(path, norm));
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		if (dynfiles[i].used && path_equal(norm, dynfiles[i].path)) {
			if (dynfiles[i].type == FS_TYPE_DIR) {
				for (int j = 0; j < FS_MEM_MAX_FILES; j++) {
					if (j != i && dynfiles[j].used &&
					    path_has_prefix_dir(dynfiles[j].path, dynfiles[i].path)) {
						return -E_INVAL;
					}
				}
			}
			dynfiles[i].used = 0;
			dynfiles[i].path[0] = '\0';
			dynfiles[i].data = NULL;
			dynfiles[i].size = 0;
			dynfiles[i].capacity = 0;
			dynfiles[i].writable = 0;
			dynfiles[i].dirty = 0;
			dynfiles[i].origin_inode = 0;
			dynfiles[i].origin_size = 0;
			return 0;
		}
	}
	if (ext4_available() && !path_hidden(norm)) {
		u32 ino;

		if (ext4_lookup_path(norm, &ino) == 0) {
			return ext4_unlink(norm);
		}
	}
	return -E_INVAL;
}

int fs_rename_path(const char *old_path, const char *new_path) {
	char old_norm[FS_PATH_MAX];
	char new_norm[FS_PATH_MAX];

	try(normalize_path(old_path, old_norm));
	try(normalize_path(new_path, new_norm));
	if (mem_lookup_path(new_norm) >= 0 || (ext4_available() && fs_stat_path(new_norm, &(struct FsStat){0}) == 0)) {
		return -E_INVAL;
	}
	if (!parent_dir_exists(new_norm)) {
		return -E_INVAL;
	}
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		if (dynfiles[i].used && path_equal(old_norm, dynfiles[i].path)) {
			strcpy(dynfiles[i].path, new_norm);
			return 0;
		}
	}
	if (ext4_available() && !path_hidden(old_norm)) {
		u32 ino;

		if (ext4_lookup_path(old_norm, &ino) == 0) {
			(void)ino;
			return ext4_rename(old_norm, new_norm);
		}
	}
	return -E_INVAL;
}

int fs_mkdir_path(const char *path) {
	char norm[FS_PATH_MAX];

	try(normalize_path(path, norm));
	if (mem_lookup_path(norm) >= 0) {
		return -E_INVAL;
	}
	if (ext4_available() && !path_hidden(norm)) {
		u32 ino;

		if (ext4_lookup_path(norm, &ino) == 0) {
			return -E_INVAL;
		}
	}
	if (!parent_dir_exists(norm)) {
		return -E_INVAL;
	}
	if (ext4_available()) {
		u32 ino;

		if (ext4_mkdir(norm, FS_MODE_DIR_DEFAULT, &ino) == 0) {
			return 0;
		}
	}
	return dyn_alloc_path(norm, FS_TYPE_DIR, 1) < 0 ? -E_NO_MEM : 0;
}

int fs_chmod_path(const char *path, u32 mode) {
	char norm[FS_PATH_MAX];
	int fileid;

	try(normalize_path(path, norm));
	fileid = mem_lookup_path(norm);
	if (fileid >= 0) {
		struct MemFile *file = file_by_id(fileid);

		if (file == NULL) {
			return -E_INVAL;
		}
		file->mode = mode & 0777;
		return 0;
	}
	if (ext4_available() && !path_hidden(norm)) {
		u32 ino;

		if (ext4_lookup_path(norm, &ino) == 0) {
			return ext4_chmod(ino, mode);
		}
	}
	return -E_INVAL;
}

int fs_sync(void) {
	for (int i = 0; i < FS_MEM_MAX_FILES; i++) {
		struct MemFile *file = &dynfiles[i];
		size_t pos = 0;
		long n;

		if (!file->used || !file->dirty || file->origin_inode == 0) {
			continue;
		}
		if (file->type != FS_TYPE_FILE) {
			return -E_INVAL;
		}
		if (ext4_truncate(file->origin_inode, file->size) < 0) {
			return -E_INVAL;
		}
		n = ext4_write(file->origin_inode, &pos, file->data, file->size);
		if (n < 0 || (size_t)n != file->size) {
			return n < 0 ? (int)n : -E_INVAL;
		}
		file->dirty = 0;
		file->origin_size = file->size;
	}
	return 0;
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
				char ext_path[FS_PATH_MAX];

				if (seen++ != wanted) {
					continue;
				}
				if (strlen(de.name) + 2 > path_len) {
					return -E_INVAL;
				}
				ext_path[0] = '/';
				strcpy(ext_path + 1, de.name);
				if (path_hidden(ext_path)) {
					wanted++;
					continue;
				}
				strcpy(path, ext_path);
				if (stat != NULL) {
					struct Ext4Stat est;

					if (ext4_stat(de.inode, &est) < 0) {
						return -E_INVAL;
					}
					stat->size = est.size;
					stat->type = est.is_dir ? FS_TYPE_DIR : FS_TYPE_FILE;
					stat->mode = est.mode;
					stat->nlink = est.nlink > 0 ? (u32)est.nlink : 1;
					stat->uid = 0;
					stat->gid = 0;
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
		stat->mode = file->mode;
		stat->nlink = file->nlink;
		stat->uid = file->uid;
		stat->gid = file->gid;
	}
	return 1;
}
