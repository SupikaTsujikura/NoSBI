#include <arch/vm.h>
#include <block.h>
#include <error.h>
#include <string.h>

#include "fsreq.h"
#include "lib.h"

#define SERV_MAX_FILES 32
#define SERV_MAX_NODES 64
#define SERV_MAX_ALIASES 64
#define SERV_MAX_CACHE 128
#define SERV_MAX_DISK_CACHE 32
#define SERV_BLOCK_SIZE PAGE_SIZE

struct ServNode {
	int used;
	int backend_fd;
	int parent;
	u32 type;
	u32 mode;
	u32 nlink;
	u32 uid;
	u32 gid;
	u_long size;
	u32 ref;
	int dirty;
	int removed;
	char path[FS_PATH_MAX];
	char name[FS_PATH_MAX];
	char symlink_target[FS_PATH_MAX];
};

struct ServAlias {
	int used;
	int nodeid;
	int parent;
	char path[FS_PATH_MAX];
	char name[FS_PATH_MAX];
};

struct ServFile {
	int used;
	int nodeid;
	u32 omode;
	u_long offset;
	u32 ref;
	char path[FS_PATH_MAX];
};

struct CacheBlock {
	int used;
	int dirty;
	int mapped;
	int nodeid;
	u_long block_no;
	u_long valid;
	char *data;
};

struct DiskSector {
	int used;
	int dirty;
	u_long sector;
	u_long stamp;
	u8 data[BLOCK_SECTOR_SIZE];
};

static union {
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
} reqpage __attribute__((aligned(PAGE_SIZE)));

static struct ServFile serv_files[SERV_MAX_FILES];
static struct ServNode serv_nodes[SERV_MAX_NODES];
static struct ServAlias serv_aliases[SERV_MAX_ALIASES];
static struct CacheBlock cache_blocks[SERV_MAX_CACHE];
static struct DiskSector disk_cache[SERV_MAX_DISK_CACHE];
static char cache_pages[SERV_MAX_CACHE][SERV_BLOCK_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u_long cache_clock;
static u_long disk_clock;
static void *reply_page;
static u_long reply_perm;

static u_long fsserv_strlen(const char *s) {
	u_long n = 0;

	while (s[n] != '\0') {
		n++;
	}
	return n;
}

static void fsserv_puts(const char *s) {
	(void)syscall_print_cons(s, fsserv_strlen(s));
}

static int disk_flush_sector(struct DiskSector *entry) {
	int r;

	if (entry == 0 || !entry->used || !entry->dirty) {
		return 0;
	}
	r = syscall_block_write(entry->sector, entry->data);
	if (r < 0) {
		return r;
	}
	entry->dirty = 0;
	return 0;
}

static struct DiskSector *disk_get_sector(u_long sector, int load) {
	struct DiskSector *victim = 0;

	for (int i = 0; i < SERV_MAX_DISK_CACHE; i++) {
		if (disk_cache[i].used && disk_cache[i].sector == sector) {
			disk_cache[i].stamp = ++disk_clock;
			return &disk_cache[i];
		}
		if (!disk_cache[i].used) {
			victim = &disk_cache[i];
		}
	}
	if (victim == 0) {
		victim = &disk_cache[0];
		for (int i = 1; i < SERV_MAX_DISK_CACHE; i++) {
			if (disk_cache[i].stamp < victim->stamp) {
				victim = &disk_cache[i];
			}
		}
		if (disk_flush_sector(victim) < 0) {
			return 0;
		}
	}
	memset(victim, 0, sizeof(*victim));
	victim->used = 1;
	victim->sector = sector;
	victim->stamp = ++disk_clock;
	if (load && syscall_block_read(sector, victim->data) < 0) {
		victim->used = 0;
		return 0;
	}
	return victim;
}

static int disk_flush_all(void) {
	for (int i = 0; i < SERV_MAX_DISK_CACHE; i++) {
		int r = disk_flush_sector(&disk_cache[i]);

		if (r < 0) {
			return r;
		}
	}
	return 0;
}

static void disk_probe_ext4(void) {
	struct DiskSector *super_sector = disk_get_sector(2, 1);

	if (super_sector != 0 && super_sector->data[56] == 0x53 &&
	    super_sector->data[57] == 0xef) {
		fsserv_puts("[fsserv-disk-ext4]");
	}
}

static struct ServFile *serv_file(int fileid) {
	if (fileid < 0 || fileid >= SERV_MAX_FILES || !serv_files[fileid].used) {
		return 0;
	}
	return &serv_files[fileid];
}

static struct ServNode *serv_node(int nodeid) {
	if (nodeid < 0 || nodeid >= SERV_MAX_NODES || !serv_nodes[nodeid].used) {
		return 0;
	}
	return &serv_nodes[nodeid];
}

static int node_lookup_path(const char *path);

static void node_to_stat(const struct ServNode *node, struct FsStat *st) {
	memset(st, 0, sizeof(*st));
	st->size = node->size;
	st->type = node->type;
	st->mode = node->mode;
	st->nlink = node->nlink;
	st->uid = node->uid;
	st->gid = node->gid;
}

static void node_init_metadata(struct ServNode *node, u32 type) {
	node->type = type;
	node->mode = type == FS_TYPE_DIR ? FS_MODE_DIR_DEFAULT : FS_MODE_FILE_DEFAULT;
	node->nlink = 1;
	node->uid = 0;
	node->gid = 0;
}

static int stat_path_follow(const char *path, struct FsStat *st, int depth) {
	int nodeid;

	if (depth > 8) {
		return -E_INVAL;
	}
	nodeid = node_lookup_path(path);
	if (nodeid >= 0) {
		struct ServNode *node = &serv_nodes[nodeid];

		if (node->type == FS_TYPE_SYMLINK) {
			return stat_path_follow(node->symlink_target, st, depth + 1);
		}
		node_to_stat(node, st);
		return 0;
	}
	return syscall_fs_stat(path, st);
}

static int node_lookup_path(const char *path) {
	for (int i = 0; i < SERV_MAX_NODES; i++) {
		if (serv_nodes[i].used && !serv_nodes[i].removed &&
		    strcmp(serv_nodes[i].path, path) == 0) {
			return i;
		}
	}
	for (int i = 0; i < SERV_MAX_ALIASES; i++) {
		if (serv_aliases[i].used && strcmp(serv_aliases[i].path, path) == 0) {
			return serv_aliases[i].nodeid;
		}
	}
	return -E_INVAL;
}

static int alias_lookup_path(const char *path) {
	for (int i = 0; i < SERV_MAX_ALIASES; i++) {
		if (serv_aliases[i].used && strcmp(serv_aliases[i].path, path) == 0) {
			return i;
		}
	}
	return -E_INVAL;
}

static int alias_alloc(int nodeid, int parent, const char *path, const char *name) {
	for (int i = 0; i < SERV_MAX_ALIASES; i++) {
		if (!serv_aliases[i].used) {
			memset(&serv_aliases[i], 0, sizeof(serv_aliases[i]));
			serv_aliases[i].used = 1;
			serv_aliases[i].nodeid = nodeid;
			serv_aliases[i].parent = parent;
			strcpy(serv_aliases[i].path, path);
			strcpy(serv_aliases[i].name, name);
			return i;
		}
	}
	return -E_NO_MEM;
}

static int node_alloc(void) {
	for (int i = 0; i < SERV_MAX_NODES; i++) {
		if (!serv_nodes[i].used) {
			memset(&serv_nodes[i], 0, sizeof(serv_nodes[i]));
			serv_nodes[i].used = 1;
			serv_nodes[i].backend_fd = -1;
			serv_nodes[i].parent = -1;
			serv_nodes[i].mode = FS_MODE_FILE_DEFAULT;
			serv_nodes[i].nlink = 1;
			return i;
		}
	}
	return -E_NO_MEM;
}

static int normalize_path(const char *path, char out[FS_PATH_MAX]) {
	u_long oi = 0;
	int last_slash = 0;

	if (path == 0 || path[0] == '\0') {
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

static int split_parent_name(const char *path, char parent[FS_PATH_MAX],
                             char name[FS_PATH_MAX]) {
	u_long len;
	u_long slash;

	if (path == 0 || strcmp(path, "/") == 0) {
		return -E_INVAL;
	}
	len = strlen(path);
	slash = len;
	while (slash > 0 && path[slash - 1] != '/') {
		slash--;
	}
	if (slash == 0) {
		return -E_INVAL;
	}
	if (slash == 1) {
		strcpy(parent, "/");
	} else {
		u_long n = slash - 1;

		if (n >= FS_PATH_MAX) {
			return -E_INVAL;
		}
		memcpy(parent, path, n);
		parent[n] = '\0';
	}
	if (strlen(path + slash) == 0 || strlen(path + slash) >= FS_PATH_MAX) {
		return -E_INVAL;
	}
	strcpy(name, path + slash);
	return 0;
}

static int path_has_prefix_dir(const char *path, const char *prefix) {
	u_long len = strlen(prefix);

	if (strcmp(prefix, "/") == 0) {
		return strcmp(path, "/") != 0;
	}
	if (strlen(path) <= len || path[len] != '/') {
		return 0;
	}
	for (u_long i = 0; i < len; i++) {
		if (path[i] != prefix[i]) {
			return 0;
		}
	}
	return 1;
}

static int node_set_path_fields(int nodeid, const char *path) {
	char parent_path[FS_PATH_MAX];
	char name[FS_PATH_MAX];
	int parent = -1;
	int r;

	if (nodeid < 0 || nodeid >= SERV_MAX_NODES) {
		return -E_INVAL;
	}
	if (strcmp(path, "/") == 0) {
		serv_nodes[nodeid].parent = -1;
		strcpy(serv_nodes[nodeid].name, "/");
		strcpy(serv_nodes[nodeid].path, "/");
		return 0;
	}
	r = split_parent_name(path, parent_path, name);
	if (r < 0) {
		return r;
	}
	parent = node_lookup_path(parent_path);
	serv_nodes[nodeid].parent = parent;
	strcpy(serv_nodes[nodeid].name, name);
	strcpy(serv_nodes[nodeid].path, path);
	return 0;
}

static int materialize_dir(const char *path) {
	struct FsStat st;
	int nodeid;
	int r;

	nodeid = node_lookup_path(path);
	if (nodeid >= 0) {
		return serv_nodes[nodeid].type == FS_TYPE_DIR ? nodeid : -E_INVAL;
	}
	r = syscall_fs_stat(path, &st);
	if (r < 0 || st.type != FS_TYPE_DIR) {
		return r < 0 ? r : -E_INVAL;
	}
	nodeid = node_alloc();
	if (nodeid < 0) {
		return nodeid;
	}
	serv_nodes[nodeid].type = FS_TYPE_DIR;
	serv_nodes[nodeid].mode = st.mode != 0 ? st.mode : FS_MODE_DIR_DEFAULT;
	serv_nodes[nodeid].nlink = st.nlink != 0 ? st.nlink : 1;
	serv_nodes[nodeid].uid = st.uid;
	serv_nodes[nodeid].gid = st.gid;
	serv_nodes[nodeid].size = st.size;
	serv_nodes[nodeid].ref = 0;
	r = node_set_path_fields(nodeid, path);
	if (r < 0) {
		memset(&serv_nodes[nodeid], 0, sizeof(serv_nodes[nodeid]));
		return r;
	}
	return nodeid;
}

static int parent_dir_for(const char *path) {
	char parent_path[FS_PATH_MAX];
	char name[FS_PATH_MAX];

	if (split_parent_name(path, parent_path, name) < 0) {
		return -E_INVAL;
	}
	(void)name;
	return materialize_dir(parent_path);
}

static int fs_path_exists(const char *path) {
	struct FsStat st;

	if (node_lookup_path(path) >= 0) {
		return 1;
	}
	return syscall_fs_stat(path, &st) == 0;
}

static int node_has_children(int nodeid) {
	for (int i = 0; i < SERV_MAX_NODES; i++) {
		if (serv_nodes[i].used && !serv_nodes[i].removed &&
		    serv_nodes[i].parent == nodeid) {
			return 1;
		}
	}
	for (int i = 0; i < SERV_MAX_ALIASES; i++) {
		if (serv_aliases[i].used && serv_aliases[i].parent == nodeid) {
			return 1;
		}
	}
	return 0;
}

static int backend_seek(int backend_fd, u_long off) {
	return syscall_fs_seek(backend_fd, off);
}

static int backend_read_at(int backend_fd, u_long off, void *buf, u_long len) {
	int r;

	r = backend_seek(backend_fd, off);
	if (r < 0) {
		return r;
	}
	return syscall_fs_read(backend_fd, buf, len);
}

static int backend_write_at(int backend_fd, u_long off, const void *buf, u_long len) {
	int r;

	r = backend_seek(backend_fd, off);
	if (r < 0) {
		return r;
	}
	return syscall_fs_write(backend_fd, buf, len);
}

static int flush_block(struct CacheBlock *blk) {
	struct ServNode *node;
	int r;

	if (blk == 0 || !blk->used || !blk->dirty) {
		return 0;
	}
	node = serv_node(blk->nodeid);
	if (node == 0 || node->backend_fd < 0 || node->removed) {
		blk->dirty = 0;
		return -E_INVAL;
	}
	r = backend_write_at(node->backend_fd, blk->block_no * SERV_BLOCK_SIZE,
	                     blk->data, blk->valid);
	if (r < 0) {
		return r;
	}
	if ((u_long)r != blk->valid) {
		return -E_INVAL;
	}
	blk->dirty = 0;
	return 0;
}

static int flush_node(int nodeid) {
	for (int i = 0; i < SERV_MAX_CACHE; i++) {
		if (cache_blocks[i].used && cache_blocks[i].nodeid == nodeid) {
			int r = flush_block(&cache_blocks[i]);

			if (r < 0) {
				return r;
			}
		}
	}
	if (nodeid >= 0 && nodeid < SERV_MAX_NODES && serv_nodes[nodeid].used) {
		serv_nodes[nodeid].dirty = 0;
	}
	return 0;
}

static int flush_file(int fileid) {
	struct ServFile *file = serv_file(fileid);

	return file == 0 ? -E_INVAL : flush_node(file->nodeid);
}

static int flush_path(const char *path) {
	int nodeid = node_lookup_path(path);

	if (nodeid < 0) {
		return 0;
	}
	return flush_node(nodeid);
}

static void invalidate_file_tail(int fileid, u_long new_size) {
	struct ServFile *file = serv_file(fileid);

	if (file == 0) {
		return;
	}
	for (int i = 0; i < SERV_MAX_CACHE; i++) {
		struct CacheBlock *blk = &cache_blocks[i];
		u_long block_start;
		u_long keep;

		if (!blk->used || blk->nodeid != file->nodeid) {
			continue;
		}
		block_start = blk->block_no * SERV_BLOCK_SIZE;
		if (block_start >= new_size) {
			if (flush_block(blk) < 0) {
				continue;
			}
			blk->used = 0;
			blk->dirty = 0;
			blk->mapped = 0;
			continue;
		}
		keep = new_size - block_start;
		if (keep < blk->valid) {
			memset(blk->data + keep, 0, blk->valid - keep);
			blk->valid = keep;
			blk->dirty = 1;
		}
	}
}

static void rename_cached_path(const char *old_path, const char *new_path) {
	int nodeid = node_lookup_path(old_path);

	if (nodeid >= 0) {
		char old_prefix[FS_PATH_MAX];

		strcpy(old_prefix, serv_nodes[nodeid].path);
		(void)node_set_path_fields(nodeid, new_path);
		for (int i = 0; i < SERV_MAX_NODES; i++) {
			char suffix[FS_PATH_MAX];
			char moved[FS_PATH_MAX];

			if (i == nodeid || !serv_nodes[i].used || serv_nodes[i].removed ||
			    !path_has_prefix_dir(serv_nodes[i].path, old_prefix)) {
				continue;
			}
			strcpy(suffix, serv_nodes[i].path + strlen(old_prefix));
			if (strlen(new_path) + strlen(suffix) + 1 > FS_PATH_MAX) {
				continue;
			}
			strcpy(moved, new_path);
			strcpy(moved + strlen(moved), suffix);
			strcpy(serv_nodes[i].path, moved);
		}
	}
	for (int i = 0; i < SERV_MAX_ALIASES; i++) {
		char suffix[FS_PATH_MAX];
		char moved[FS_PATH_MAX];

		if (!serv_aliases[i].used) {
			continue;
		}
		if (strcmp(serv_aliases[i].path, old_path) == 0) {
			strcpy(serv_aliases[i].path, new_path);
		} else if (path_has_prefix_dir(serv_aliases[i].path, old_path)) {
			strcpy(suffix, serv_aliases[i].path + strlen(old_path));
			if (strlen(new_path) + strlen(suffix) + 1 <= FS_PATH_MAX) {
				strcpy(moved, new_path);
				strcpy(moved + strlen(moved), suffix);
				strcpy(serv_aliases[i].path, moved);
			}
		}
	}
	for (int i = 0; i < SERV_MAX_FILES; i++) {
		if (!serv_files[i].used) {
			continue;
		}
		if (strcmp(serv_files[i].path, old_path) == 0) {
			strcpy(serv_files[i].path, new_path);
		} else if (path_has_prefix_dir(serv_files[i].path, old_path)) {
			char suffix[FS_PATH_MAX];
			char moved[FS_PATH_MAX];

			strcpy(suffix, serv_files[i].path + strlen(old_path));
			if (strlen(new_path) + strlen(suffix) + 1 <= FS_PATH_MAX) {
				strcpy(moved, new_path);
				strcpy(moved + strlen(moved), suffix);
				strcpy(serv_files[i].path, moved);
			}
		}
	}
}

static void drop_node_if_unused(int nodeid) {
	struct ServNode *node = serv_node(nodeid);

	if (node == 0 || node->ref != 0 || !node->removed) {
		return;
	}
	for (int i = 0; i < SERV_MAX_CACHE; i++) {
		if (cache_blocks[i].used && cache_blocks[i].nodeid == nodeid) {
			cache_blocks[i].used = 0;
			cache_blocks[i].dirty = 0;
			cache_blocks[i].mapped = 0;
		}
	}
	if (node->backend_fd >= 0) {
		(void)syscall_fs_close(node->backend_fd);
	}
	memset(node, 0, sizeof(*node));
}

static int flush_all(void) {
	for (int i = 0; i < SERV_MAX_CACHE; i++) {
		int r = flush_block(&cache_blocks[i]);

		if (r < 0) {
			return r;
		}
	}
	if (disk_flush_all() < 0) {
		return -E_INVAL;
	}
	return syscall_fs_sync();
}

static struct CacheBlock *alloc_cache_block(void) {
	int victim = -1;

	for (int i = 0; i < SERV_MAX_CACHE; i++) {
		if (!cache_blocks[i].used) {
			return &cache_blocks[i];
		}
	}
	for (int tries = 0; tries < SERV_MAX_CACHE; tries++) {
		int idx = (int)(cache_clock++ % SERV_MAX_CACHE);

		if (!cache_blocks[idx].mapped) {
			victim = idx;
			break;
		}
	}
	if (victim < 0 || flush_block(&cache_blocks[victim]) < 0) {
		return 0;
	}
	cache_blocks[victim].used = 0;
	cache_blocks[victim].dirty = 0;
	cache_blocks[victim].mapped = 0;
	return &cache_blocks[victim];
}

static struct CacheBlock *get_cache_block(int fileid, u_long block_no, int load) {
	struct ServFile *file = serv_file(fileid);
	struct ServNode *node;
	struct CacheBlock *blk;
	u_long off = block_no * SERV_BLOCK_SIZE;
	int r;

	if (file == 0 || (node = serv_node(file->nodeid)) == 0) {
		return 0;
	}
	for (int i = 0; i < SERV_MAX_CACHE; i++) {
		if (cache_blocks[i].used && cache_blocks[i].nodeid == file->nodeid &&
		    cache_blocks[i].block_no == block_no) {
			return &cache_blocks[i];
		}
	}
	blk = alloc_cache_block();
	if (blk == 0) {
		return 0;
	}
	memset(blk, 0, sizeof(*blk));
	blk->used = 1;
	blk->nodeid = file->nodeid;
	blk->block_no = block_no;
	blk->data = cache_pages[blk - cache_blocks];
	memset(blk->data, 0, SERV_BLOCK_SIZE);
	if (load && off < node->size) {
		u_long want = node->size - off;

		if (want > SERV_BLOCK_SIZE) {
			want = SERV_BLOCK_SIZE;
		}
		r = backend_read_at(node->backend_fd, off, blk->data, want);
		if (r < 0) {
			blk->used = 0;
			return 0;
		}
		blk->valid = (u_long)r;
	} else {
		blk->valid = 0;
	}
	return blk;
}

static int serv_alloc_file(void) {
	for (int i = 0; i < SERV_MAX_FILES; i++) {
		if (!serv_files[i].used) {
			memset(&serv_files[i], 0, sizeof(serv_files[i]));
			serv_files[i].used = 1;
			serv_files[i].ref = 1;
			return i;
		}
	}
	return -E_NO_MEM;
}

static int node_open_backend(const char *path, u32 omode) {
	struct FsStat st;
	struct ServNode *node;
	char norm[FS_PATH_MAX];
	char resolved[FS_PATH_MAX];
	int nodeid;
	int backend;
	int r;
	int symlink_depth = 0;

	r = normalize_path(path, norm);
	if (r < 0) {
		return r;
	}
	strcpy(resolved, norm);

resolve_again:
	path = resolved;

	nodeid = node_lookup_path(path);
	if (nodeid >= 0) {
		node = &serv_nodes[nodeid];
		if (node->type == FS_TYPE_SYMLINK) {
			if (++symlink_depth > 8 || node->symlink_target[0] == '\0') {
				return -E_INVAL;
			}
			strcpy(resolved, node->symlink_target);
			goto resolve_again;
		}
		if ((omode & FS_OPEN_TRUNC) && node->type == FS_TYPE_DIR) {
			return -E_INVAL;
		}
		if ((omode & FS_OPEN_TRUNC) && node->type == FS_TYPE_FILE) {
			int r = flush_node(nodeid);

			if (r < 0) {
				return r;
			}
			r = syscall_fs_truncate(node->backend_fd, 0);
			if (r < 0) {
				return r;
			}
			node->size = 0;
			node->dirty = 1;
			for (int i = 0; i < SERV_MAX_CACHE; i++) {
				if (cache_blocks[i].used && cache_blocks[i].nodeid == nodeid) {
					cache_blocks[i].used = 0;
					cache_blocks[i].dirty = 0;
					cache_blocks[i].mapped = 0;
				}
			}
		}
		node->ref++;
		return nodeid;
	}

	if (strcmp(path, "/") != 0) {
		int parent = parent_dir_for(path);

		if (parent < 0) {
			return parent;
		}
	}
	if (syscall_fs_stat(path, &st) == 0 && st.type == FS_TYPE_DIR) {
		nodeid = materialize_dir(path);
		if (nodeid < 0) {
			return nodeid;
		}
		serv_nodes[nodeid].ref++;
		return nodeid;
	}
	backend = syscall_fs_open_flags(path, (int)omode);
	if (backend < 0) {
		return backend;
	}
	nodeid = node_alloc();
	if (nodeid < 0) {
		(void)syscall_fs_close(backend);
		return nodeid;
	}
	node = &serv_nodes[nodeid];
	node->backend_fd = backend;
	node_init_metadata(node, FS_TYPE_FILE);
	node->ref = 1;
	r = node_set_path_fields(nodeid, path);
	if (r < 0) {
		(void)syscall_fs_close(backend);
		memset(node, 0, sizeof(*node));
		return r;
	}
	if (syscall_fs_fstat(backend, &st) == 0) {
		node->size = st.size;
		node->type = st.type;
		node->mode = st.mode != 0 ? st.mode :
		             (st.type == FS_TYPE_DIR ? FS_MODE_DIR_DEFAULT : FS_MODE_FILE_DEFAULT);
		node->nlink = st.nlink != 0 ? st.nlink : 1;
		node->uid = st.uid;
		node->gid = st.gid;
	}
	return nodeid;
}

static int node_register_dir(const char *path) {
	char norm[FS_PATH_MAX];
	int parent;
	int nodeid;
	struct ServNode *node;
	int r;

	r = normalize_path(path, norm);
	if (r < 0) {
		return r;
	}
	if (node_lookup_path(norm) >= 0) {
		return -E_INVAL;
	}
	parent = parent_dir_for(norm);
	if (parent < 0) {
		return parent;
	}
	nodeid = node_alloc();
	if (nodeid < 0) {
		return nodeid;
	}
	node = &serv_nodes[nodeid];
	node->backend_fd = -1;
	node_init_metadata(node, FS_TYPE_DIR);
	node->size = 0;
	node->ref = 0;
	r = node_set_path_fields(nodeid, norm);
	if (r < 0) {
		memset(node, 0, sizeof(*node));
		return r;
	}
	return 0;
}

static int serv_open(void) {
	int fileid;
	int nodeid;
	struct ServFile *file;

	nodeid = node_open_backend(reqpage.open.req_path, reqpage.open.req_omode);
	if (nodeid < 0) {
		return nodeid;
	}
	fileid = serv_alloc_file();
	if (fileid < 0) {
		if (serv_nodes[nodeid].ref > 0) {
			serv_nodes[nodeid].ref--;
		}
		if (serv_nodes[nodeid].ref == 0 && !serv_nodes[nodeid].dirty) {
			(void)syscall_fs_close(serv_nodes[nodeid].backend_fd);
			memset(&serv_nodes[nodeid], 0, sizeof(serv_nodes[nodeid]));
		}
		return fileid;
	}
	file = &serv_files[fileid];
	file->nodeid = nodeid;
	file->omode = reqpage.open.req_omode;
	strcpy(file->path, reqpage.open.req_path);
	if (file->omode & FS_OPEN_APPEND) {
		file->offset = serv_nodes[nodeid].size;
	}
	return fileid;
}

static int serv_read_into(int fileid, u_long off, void *buf, u_long len) {
	struct ServFile *file = serv_file(fileid);
	struct ServNode *node;
	u_long done = 0;

	if (file == 0 || (node = serv_node(file->nodeid)) == 0 || buf == 0) {
		return -E_INVAL;
	}
	if (off >= node->size) {
		return 0;
	}
	if (len > node->size - off) {
		len = node->size - off;
	}
	while (done < len) {
		u_long cur = off + done;
		u_long block_no = cur / SERV_BLOCK_SIZE;
		u_long block_off = cur % SERV_BLOCK_SIZE;
		u_long n = SERV_BLOCK_SIZE - block_off;
		struct CacheBlock *blk;

		if (n > len - done) {
			n = len - done;
		}
		blk = get_cache_block(fileid, block_no, 1);
		if (blk == 0) {
			return done == 0 ? -E_NO_MEM : (int)done;
		}
		if (block_off >= blk->valid) {
			break;
		}
		if (n > blk->valid - block_off) {
			n = blk->valid - block_off;
		}
		memcpy((char *)buf + done, blk->data + block_off, n);
		done += n;
	}
	return (int)done;
}

static int serv_write_from(int fileid, u_long off, const void *buf, u_long len) {
	struct ServFile *file = serv_file(fileid);
	struct ServNode *node;
	u_long done = 0;

	if (file == 0 || (node = serv_node(file->nodeid)) == 0 || buf == 0) {
		return -E_INVAL;
	}
	while (done < len) {
		u_long cur = off + done;
		u_long block_no = cur / SERV_BLOCK_SIZE;
		u_long block_off = cur % SERV_BLOCK_SIZE;
		u_long n = SERV_BLOCK_SIZE - block_off;
		struct CacheBlock *blk;

		if (n > len - done) {
			n = len - done;
		}
		blk = get_cache_block(fileid, block_no, 1);
		if (blk == 0) {
			return done == 0 ? -E_NO_MEM : (int)done;
		}
		if (block_off > blk->valid) {
			memset(blk->data + blk->valid, 0, block_off - blk->valid);
		}
		memcpy(blk->data + block_off, (const char *)buf + done, n);
		if (block_off + n > blk->valid) {
			blk->valid = block_off + n;
		}
		blk->dirty = 1;
		node->dirty = 1;
		done += n;
	}
	if (off + done > node->size) {
		node->size = off + done;
		(void)syscall_fs_truncate(node->backend_fd, node->size);
	}
	return (int)done;
}

static int serv_read(void) {
	struct ServFile *file = serv_file(reqpage.io.req_fileid);
	int r;

	if (file == 0) {
		return -E_INVAL;
	}
	r = serv_read_into(reqpage.io.req_fileid, file->offset, reqpage.io.req_buf,
	                   reqpage.io.req_len);
	if (r > 0) {
		file->offset += (u_long)r;
	}
	return r;
}

static int serv_write(void) {
	struct ServFile *file = serv_file(reqpage.io.req_fileid);
	struct ServNode *node;
	int r;

	if (file == 0 || (node = serv_node(file->nodeid)) == 0) {
		return -E_INVAL;
	}
	if (file->omode & FS_OPEN_APPEND) {
		file->offset = node->size;
	}
	r = serv_write_from(reqpage.io.req_fileid, file->offset, reqpage.io.req_buf,
	                    reqpage.io.req_len);
	if (r > 0) {
		file->offset += (u_long)r;
	}
	return r;
}

static int serv_close(void) {
	int fileid = reqpage.close.req_fileid;
	struct ServFile *file = serv_file(fileid);
	struct ServNode *node;
	int nodeid;
	int r;

	if (file == 0 || (node = serv_node(file->nodeid)) == 0) {
		return -E_INVAL;
	}
	nodeid = file->nodeid;
	r = flush_file(fileid);
	if (r < 0) {
		return r;
	}
	if (node->ref > 0) {
		node->ref--;
	}
	memset(file, 0, sizeof(*file));
	if (node->ref != 0) {
		return 0;
	}
	if (node->removed) {
		drop_node_if_unused(nodeid);
		return 0;
	}
	return 0;
}

static int serv_dirty(void) {
	struct ServFile *file = serv_file(reqpage.dirty.req_fileid);
	struct ServNode *node;
	u_long off = reqpage.dirty.req_offset;
	u_long len = reqpage.dirty.req_len;
	u_long done = 0;

	if (file == 0 || (node = serv_node(file->nodeid)) == 0) {
		return -E_INVAL;
	}
	if (len == 0) {
		return 0;
	}
	while (done < len) {
		u_long cur = off + done;
		u_long block_no = cur / SERV_BLOCK_SIZE;
		u_long block_off = cur % SERV_BLOCK_SIZE;
		u_long n = SERV_BLOCK_SIZE - block_off;
		struct CacheBlock *blk;

		if (n > len - done) {
			n = len - done;
		}
		blk = get_cache_block(reqpage.dirty.req_fileid, block_no, 1);
		if (blk == 0) {
			return -E_NO_MEM;
		}
		if (block_off + n > blk->valid) {
			blk->valid = block_off + n;
		}
		blk->dirty = 1;
		node->dirty = 1;
		done += n;
	}
	if (off + len > node->size) {
		node->size = off + len;
		(void)syscall_fs_truncate(node->backend_fd, node->size);
	}
	return 0;
}

static int backend_immediate_child(const char *dir, const char *path) {
	u_long dir_len = strlen(dir);
	const char *rest;

	if (strcmp(dir, "/") == 0) {
		if (path[0] != '/' || path[1] == '\0') {
			return 0;
		}
		rest = path + 1;
	} else {
		if (!path_has_prefix_dir(path, dir)) {
			return 0;
		}
		rest = path + dir_len + 1;
	}
	return strchr(rest, '/') == 0;
}

static int node_visible_count(int dir_nodeid) {
	int count = 0;

	for (int i = 0; i < SERV_MAX_NODES; i++) {
		if (serv_nodes[i].used && !serv_nodes[i].removed &&
		    serv_nodes[i].path[0] != '\0' && serv_nodes[i].parent == dir_nodeid) {
			count++;
		}
	}
	for (int i = 0; i < SERV_MAX_ALIASES; i++) {
		if (serv_aliases[i].used && serv_aliases[i].parent == dir_nodeid) {
			count++;
		}
	}
	return count;
}

static int serv_list_child(const char *dir_req, int index, char *out_path, u_long out_len,
                           struct FsStat *out_stat) {
	char dir[FS_PATH_MAX];
	int dir_nodeid;
	int node_count;
	int wanted;
	int seen = 0;
	int r;

	if (index < 0 || out_path == 0 || out_len == 0 || out_stat == 0) {
		return -E_INVAL;
	}
	r = normalize_path(dir_req == 0 || dir_req[0] == '\0' ? "/" : dir_req, dir);
	if (r < 0) {
		return r;
	}
	dir_nodeid = materialize_dir(dir);
	if (dir_nodeid < 0) {
		return dir_nodeid;
	}
	node_count = node_visible_count(dir_nodeid);
	if (index < node_count) {
		struct ServNode *node = 0;
		int seen_node = 0;
		const char *entry_path = 0;

		for (int i = 0; i < SERV_MAX_NODES; i++) {
			if (!serv_nodes[i].used || serv_nodes[i].removed ||
			    serv_nodes[i].path[0] == '\0' ||
			    serv_nodes[i].parent != dir_nodeid) {
				continue;
			}
			if (seen_node++ == index) {
				node = &serv_nodes[i];
				entry_path = node->path;
				break;
			}
		}
		if (node == 0) {
			for (int i = 0; i < SERV_MAX_ALIASES; i++) {
				if (!serv_aliases[i].used || serv_aliases[i].parent != dir_nodeid) {
					continue;
				}
				if (seen_node++ == index) {
					node = serv_node(serv_aliases[i].nodeid);
					entry_path = serv_aliases[i].path;
					break;
				}
			}
		}
		if (node == 0) {
			return 0;
		}
		if (strlen(entry_path) + 1 > out_len) {
			return -E_INVAL;
		}
		strcpy(out_path, entry_path);
		node_to_stat(node, out_stat);
		return 1;
	}
	wanted = index - node_count;
	for (int backend_i = 0;; backend_i++) {
		char path[FS_PATH_MAX];
		struct FsStat st;
		int r = syscall_fs_list(backend_i, path, sizeof(path), &st);

		if (r <= 0) {
			return r;
		}
		if (!backend_immediate_child(dir, path)) {
			continue;
		}
		if (node_lookup_path(path) >= 0) {
			continue;
		}
		if (seen++ != wanted) {
			continue;
		}
		if (strlen(path) + 1 > out_len) {
			return -E_INVAL;
		}
		strcpy(out_path, path);
		*out_stat = st;
		return 1;
	}
}

static int serv_list(void) {
	return serv_list_child(reqpage.list.req_dir, reqpage.list.req_index,
	                       reqpage.list.req_path, reqpage.list.req_path_len,
	                       &reqpage.list.req_stat);
}

static int serv_getdents(void) {
	struct ServFile *file = serv_file(reqpage.io.req_fileid);
	struct ServNode *node;
	struct FsDirent *ents = (struct FsDirent *)reqpage.io.req_buf;
	u_long cap = reqpage.io.req_len / sizeof(struct FsDirent);
	u_long done = 0;

	if (file == 0 || (node = serv_node(file->nodeid)) == 0 ||
	    node->type != FS_TYPE_DIR) {
		return -E_INVAL;
	}
	while (done < cap) {
		char path[FS_PATH_MAX];
		struct FsStat st;
		int r = serv_list_child(node->path, (int)file->offset, path, sizeof(path), &st);

		if (r < 0) {
			return done == 0 ? r : (int)(done * sizeof(struct FsDirent));
		}
		if (r == 0) {
			break;
		}
		ents[done].size = st.size;
		ents[done].type = st.type;
		ents[done].name_len = (u32)strlen(path);
		strcpy(ents[done].name, path);
		file->offset++;
		done++;
	}
	return (int)(done * sizeof(struct FsDirent));
}

static int handle_request(u_long req) {
	reply_page = 0;
	reply_perm = 0;
	switch (req) {
	case FSREQ_OPEN:
		return serv_open();
	case FSREQ_MAP: {
		struct ServFile *file = serv_file(reqpage.io.req_fileid);
		struct CacheBlock *blk;
		u_long block_no;
		u_long in_block;

		if (file == 0) {
			return -E_INVAL;
		}
		block_no = reqpage.io.req_offset / SERV_BLOCK_SIZE;
		in_block = reqpage.io.req_offset % SERV_BLOCK_SIZE;
		if (in_block != 0) {
			return -E_INVAL;
		}
		blk = get_cache_block(reqpage.io.req_fileid, block_no, 1);
		if (blk == 0) {
			return -E_NO_MEM;
		}
		blk->mapped = 1;
		reply_page = blk->data;
		reply_perm = PTE_D;
		return (int)blk->valid;
	}
	case FSREQ_READ:
		return serv_read();
	case FSREQ_WRITE:
		return serv_write();
	case FSREQ_SEEK: {
		struct ServFile *file = serv_file(reqpage.seek.req_fileid);

		if (file == 0) {
			return -E_INVAL;
		}
		file->offset = reqpage.seek.req_offset;
		return 0;
	}
	case FSREQ_SET_SIZE: {
		struct ServFile *file = serv_file(reqpage.set_size.req_fileid);
		struct ServNode *node;
		u_long old_size;
		int r;

		if (file == 0 || (node = serv_node(file->nodeid)) == 0) {
			return -E_INVAL;
		}
		old_size = node->size;
		r = flush_file(reqpage.set_size.req_fileid);
		if (r < 0) {
			return r;
		}
		r = syscall_fs_truncate(node->backend_fd, reqpage.set_size.req_size);
		if (r == 0) {
			node->size = reqpage.set_size.req_size;
			node->dirty = 1;
			if (node->size < old_size) {
				invalidate_file_tail(reqpage.set_size.req_fileid, node->size);
			}
			if (file->offset > node->size) {
				file->offset = node->size;
			}
		}
		return r;
	}
	case FSREQ_STAT: {
		char path[FS_PATH_MAX];
		int r = normalize_path(reqpage.stat.req_path, path);

		if (r < 0) {
			return r;
		}
		return stat_path_follow(path, &reqpage.stat.req_stat, 0);
	}
	case FSREQ_FSTAT: {
		struct ServFile *file = serv_file(reqpage.io.req_fileid);
		struct ServNode *node;
		struct FsStat st;

		if (file == 0 || (node = serv_node(file->nodeid)) == 0) {
			return -E_INVAL;
		}
		node_to_stat(node, &st);
		memcpy(reqpage.io.req_buf, &st, sizeof(st));
		return 0;
	}
	case FSREQ_CLOSE:
		return serv_close();
	case FSREQ_DIRTY:
		return serv_dirty();
	case FSREQ_REMOVE: {
		char path[FS_PATH_MAX];
		int aliasid;
		int nodeid;
		int r;

		r = normalize_path(reqpage.path.req_path, path);
		if (r < 0) {
			return r;
		}
		aliasid = alias_lookup_path(path);
		if (aliasid >= 0) {
			nodeid = serv_aliases[aliasid].nodeid;
			memset(&serv_aliases[aliasid], 0, sizeof(serv_aliases[aliasid]));
			if (serv_nodes[nodeid].nlink > 0) {
				serv_nodes[nodeid].nlink--;
			}
			if (serv_nodes[nodeid].nlink == 0) {
				serv_nodes[nodeid].removed = 1;
				drop_node_if_unused(nodeid);
			}
			return 0;
		}
		nodeid = node_lookup_path(path);
		if (nodeid >= 0 && serv_nodes[nodeid].type == FS_TYPE_DIR &&
		    node_has_children(nodeid)) {
			return -E_INVAL;
		}
		r = nodeid >= 0 ? flush_node(nodeid) : flush_path(path);
		if (r < 0) {
			return r;
		}
		if (nodeid >= 0 && serv_nodes[nodeid].nlink > 1) {
			serv_nodes[nodeid].nlink--;
			serv_nodes[nodeid].path[0] = '\0';
			serv_nodes[nodeid].name[0] = '\0';
			serv_nodes[nodeid].parent = -1;
			return 0;
		}
		r = syscall_fs_unlink(path);
		if (nodeid >= 0 && (r == 0 || r < 0)) {
			serv_nodes[nodeid].removed = 1;
			serv_nodes[nodeid].path[0] = '\0';
			drop_node_if_unused(nodeid);
			return 0;
		}
		return r;
	}
	case FSREQ_RENAME: {
		char old_path[FS_PATH_MAX];
		char new_path[FS_PATH_MAX];
		char parent_path[FS_PATH_MAX];
		char name[FS_PATH_MAX];
		int aliasid;
		int nodeid;
		int parent;
		int r;

		r = normalize_path(reqpage.rename.req_old_path, old_path);
		if (r < 0) {
			return r;
		}
		r = normalize_path(reqpage.rename.req_new_path, new_path);
		if (r < 0) {
			return r;
		}
		if (strcmp(old_path, "/") == 0 || fs_path_exists(new_path)) {
			return -E_INVAL;
		}
		parent = parent_dir_for(new_path);
		if (parent < 0) {
			return parent;
		}
		nodeid = node_lookup_path(old_path);
		if (nodeid >= 0 && serv_nodes[nodeid].type == FS_TYPE_DIR &&
		    path_has_prefix_dir(new_path, old_path)) {
			return -E_INVAL;
		}
		r = nodeid >= 0 ? flush_node(nodeid) : flush_path(old_path);
		if (r < 0) {
			return r;
		}
		aliasid = alias_lookup_path(old_path);
		if (aliasid >= 0) {
			if (split_parent_name(new_path, parent_path, name) < 0) {
				return -E_INVAL;
			}
			serv_aliases[aliasid].parent = parent;
			strcpy(serv_aliases[aliasid].path, new_path);
			strcpy(serv_aliases[aliasid].name, name);
			return 0;
		}
		r = syscall_fs_rename(old_path, new_path);
		if (r == 0) {
			rename_cached_path(old_path, new_path);
		}
		return r;
	}
	case FSREQ_MKDIR:
		{
			char path[FS_PATH_MAX];
			int parent;
			int r = normalize_path(reqpage.path.req_path, path);

			if (r < 0) {
				return r;
			}
			if (fs_path_exists(path)) {
				return -E_INVAL;
			}
			parent = parent_dir_for(path);
			if (parent < 0) {
				return parent;
			}
			(void)parent;
			r = syscall_fs_mkdir(path);
			if (r < 0) {
				return r;
			}
			return node_register_dir(path);
		}
	case FSREQ_LINK:
		{
			char old_path[FS_PATH_MAX];
			char new_path[FS_PATH_MAX];
			char parent_path[FS_PATH_MAX];
			char name[FS_PATH_MAX];
			int nodeid;
			int parent;
			int r;

			r = normalize_path(reqpage.link.req_old_path, old_path);
			if (r < 0) {
				return r;
			}
			r = normalize_path(reqpage.link.req_new_path, new_path);
			if (r < 0 || fs_path_exists(new_path)) {
				return -E_INVAL;
			}
			nodeid = node_lookup_path(old_path);
			if (nodeid < 0) {
				nodeid = node_open_backend(old_path, 0);
				if (nodeid < 0) {
					return nodeid;
				}
				if (serv_nodes[nodeid].ref > 0) {
					serv_nodes[nodeid].ref--;
				}
			}
			if (serv_nodes[nodeid].type == FS_TYPE_DIR) {
				return -E_INVAL;
			}
			parent = parent_dir_for(new_path);
			if (parent < 0 ||
			    split_parent_name(new_path, parent_path, name) < 0) {
				return -E_INVAL;
			}
			r = alias_alloc(nodeid, parent, new_path, name);
			if (r < 0) {
				return r;
			}
			serv_nodes[nodeid].nlink++;
			return 0;
		}
	case FSREQ_SYMLINK:
		{
			char link_path[FS_PATH_MAX];
			char parent_path[FS_PATH_MAX];
			char name[FS_PATH_MAX];
			int nodeid;
			int parent;
			int r = normalize_path(reqpage.symlink.req_link_path, link_path);

			if (r < 0 || fs_path_exists(link_path) ||
			    strlen(reqpage.symlink.req_target) >= FS_PATH_MAX) {
				return -E_INVAL;
			}
			parent = parent_dir_for(link_path);
			if (parent < 0 ||
			    split_parent_name(link_path, parent_path, name) < 0) {
				return -E_INVAL;
			}
			nodeid = node_alloc();
			if (nodeid < 0) {
				return nodeid;
			}
			node_init_metadata(&serv_nodes[nodeid], FS_TYPE_SYMLINK);
			serv_nodes[nodeid].mode = 0777;
			serv_nodes[nodeid].size = strlen(reqpage.symlink.req_target);
			serv_nodes[nodeid].parent = parent;
			strcpy(serv_nodes[nodeid].path, link_path);
			strcpy(serv_nodes[nodeid].name, name);
			strcpy(serv_nodes[nodeid].symlink_target, reqpage.symlink.req_target);
			return 0;
		}
	case FSREQ_READLINK:
		{
			char path[FS_PATH_MAX];
			int nodeid;
			u_long n;
			int r = normalize_path(reqpage.readlink.req_path, path);

			if (r < 0) {
				return r;
			}
			nodeid = node_lookup_path(path);
			if (nodeid < 0 || serv_nodes[nodeid].type != FS_TYPE_SYMLINK) {
				return -E_INVAL;
			}
			n = strlen(serv_nodes[nodeid].symlink_target);
			if (n > reqpage.readlink.req_len) {
				n = reqpage.readlink.req_len;
			}
			memcpy(reqpage.readlink.req_target,
			       serv_nodes[nodeid].symlink_target, n);
			return (int)n;
		}
	case FSREQ_CHMOD:
		{
			char path[FS_PATH_MAX];
			int nodeid;
			int r = normalize_path(reqpage.chmod.req_path, path);

			if (r < 0) {
				return r;
			}
			nodeid = node_lookup_path(path);
			if (nodeid >= 0 && serv_nodes[nodeid].type == FS_TYPE_SYMLINK) {
				strcpy(path, serv_nodes[nodeid].symlink_target);
				nodeid = node_lookup_path(path);
			}
			if (nodeid < 0) {
				nodeid = node_open_backend(path, 0);
				if (nodeid < 0) {
					return nodeid;
				}
				if (serv_nodes[nodeid].ref > 0) {
					serv_nodes[nodeid].ref--;
				}
			}
			serv_nodes[nodeid].mode = reqpage.chmod.req_mode & 0777;
			serv_nodes[nodeid].dirty = 1;
			if (serv_nodes[nodeid].path[0] != '\0' &&
			    strcmp(path, serv_nodes[nodeid].path) == 0 &&
			    syscall_fs_chmod(path, serv_nodes[nodeid].mode) < 0 &&
			    serv_nodes[nodeid].backend_fd >= 0) {
				return -E_INVAL;
			}
			return 0;
		}
	case FSREQ_SYNC:
		return flush_all();
	case FSREQ_LIST:
		return serv_list();
	case FSREQ_GETDENTS:
		return serv_getdents();
	default:
		return -E_INVAL;
	}
}

void user_main(long arg, char **argv) {
	(void)arg;
	(void)argv;
	fsserv_puts("[fsserv-start]");
	disk_probe_ext4();
	for (;;) {
		u_long from = 0;
		u_long req = 0;
		u_long perm = 0;
		int r = ipc_recv(&from, &req, &reqpage, &perm);

		if (r < 0) {
			continue;
		}
		r = handle_request(req);
		(void)ipc_send(from, (u_long)r, reply_page, reply_perm);
	}
}
