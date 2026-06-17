#include <block.h>
#include <error.h>
#include <ext4.h>
#include <printk.h>
#include <string.h>

#define EXT4_SUPER_OFFSET      1024UL
#define EXT4_SUPER_MAGIC       0xef53
#define EXT4_ROOT_INO          2
#define EXT4_EXT_MAGIC         0xf30a
#define EXT4_I_BLOCK_OFF       40
#define EXT4_MAX_FILE          (8UL * 1024UL * 1024UL)
#define EXT4_FILE_CACHE_MAX    8
#define EXT4_FILE_CACHE_SIZE   (2UL * 1024UL * 1024UL)
#define EXT4_S_IFDIR           0x4000
#define EXT4_S_IFREG           0x8000

static int ext4_ready;
static u32 block_size;
static u32 blocks_per_group;
static u32 inodes_per_group;
static u16 inode_size;
static u16 desc_size;
static u64 gdt_start_block;
static u8 ext4_file_buf[EXT4_MAX_FILE];

struct Ext4NameCache {
	int used;
	u32 parent;
	u32 ino;
	char name[64];
};

struct Ext4FileCache {
	int used;
	size_t size;
	char name[64];
	u8 data[EXT4_FILE_CACHE_SIZE];
};

static struct Ext4NameCache name_cache[64];
static struct Ext4FileCache file_cache[EXT4_FILE_CACHE_MAX];

static u16 rd16(const u8 *p) {
	return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 rd32(const u8 *p) {
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 rd64_lohi(const u8 *lo, const u8 *hi) {
	return (u64)rd32(lo) | ((u64)rd32(hi) << 32);
}

static int kstrcmp(const char *a, const char *b) {
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return (u8)*a - (u8)*b;
}

static size_t kstrlen(const char *s) {
	size_t n = 0;

	while (s != NULL && s[n] != '\0') {
		n++;
	}
	return n;
}

static int name_eq(const char *a, const char *b, size_t b_len) {
	for (size_t i = 0; i < b_len; i++) {
		if (a[i] == '\0' || a[i] != b[i]) {
			return 0;
		}
	}
	return a[b_len] == '\0';
}

static void copy_name(char *dst, const char *src, size_t len, size_t max_len) {
	size_t i;

	for (i = 0; i + 1 < max_len && i < len; i++) {
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

static void cache_insert(u32 parent, u32 ino, const char *name, size_t len) {
	if (ino == 0 || len == 0 || len >= sizeof(name_cache[0].name)) {
		return;
	}
	for (size_t i = 0; i < ARRAY_SIZE(name_cache); i++) {
		if (!name_cache[i].used ||
		    (name_cache[i].parent == parent && name_eq(name_cache[i].name, name, len))) {
			name_cache[i].used = 1;
			name_cache[i].parent = parent;
			name_cache[i].ino = ino;
			copy_name(name_cache[i].name, name, len, sizeof(name_cache[i].name));
			return;
		}
	}
}

static int cache_lookup(u32 parent, const char *name, u32 *ino) {
	for (size_t i = 0; i < ARRAY_SIZE(name_cache); i++) {
		if (name_cache[i].used && name_cache[i].parent == parent &&
		    kstrcmp(name_cache[i].name, name) == 0) {
			*ino = name_cache[i].ino;
			return 0;
		}
	}
	return -E_INVAL;
}

static const char *path_to_cache_name(const char *path) {
	const char *p = path;

	if (p == NULL) {
		return NULL;
	}
	while (*p == '/') {
		p++;
	}
	while (p[0] == '.' && p[1] == '/') {
		p += 2;
	}
	return p;
}

static int file_cache_lookup(const char *path, const void **image, size_t *size) {
	const char *name = path_to_cache_name(path);

	if (name == NULL || image == NULL || size == NULL) {
		return -E_INVAL;
	}
	for (size_t i = 0; i < ARRAY_SIZE(file_cache); i++) {
		if (file_cache[i].used && kstrcmp(file_cache[i].name, name) == 0) {
			*image = file_cache[i].data;
			*size = file_cache[i].size;
			return 0;
		}
	}
	return -E_INVAL;
}

static int read_sectors(u64 sector, void *buf, size_t count) {
	for (size_t i = 0; i < count; i++) {
		if (block_read_sector(sector + i, (u8 *)buf + i * BLOCK_SECTOR_SIZE) < 0) {
			return -E_INVAL;
		}
	}
	return 0;
}

static int read_bytes(u64 offset, void *buf, size_t size) {
	static u8 sector_buf[BLOCK_SECTOR_SIZE];
	size_t done = 0;

	while (done < size) {
		u64 sector = (offset + done) / BLOCK_SECTOR_SIZE;
		size_t off = (size_t)((offset + done) % BLOCK_SECTOR_SIZE);
		size_t n = BLOCK_SECTOR_SIZE - off;

		if (n > size - done) {
			n = size - done;
		}
		if (block_read_sector(sector, sector_buf) < 0) {
			return -E_INVAL;
		}
		memcpy((u8 *)buf + done, sector_buf + off, n);
		done += n;
	}
	return 0;
}

static int read_block(u64 block, void *buf) {
	return read_sectors(block * (block_size / BLOCK_SECTOR_SIZE), buf,
	                    block_size / BLOCK_SECTOR_SIZE);
}

static u64 inode_size_bytes(const u8 *inode) {
	return rd64_lohi(inode + 4, inode + 108);
}

static int inode_is_dir(const u8 *inode) {
	return (rd16(inode) & EXT4_S_IFDIR) == EXT4_S_IFDIR;
}

static int inode_is_reg(const u8 *inode) {
	return (rd16(inode) & EXT4_S_IFREG) == EXT4_S_IFREG;
}

static int read_inode(u32 ino, u8 *inode) {
	u32 group;
	u32 index;
	u64 desc_off;
	u8 desc[64];
	u64 inode_table;
	u64 inode_off;

	if (!ext4_ready || ino == 0) {
		return -E_INVAL;
	}
	group = (ino - 1) / inodes_per_group;
	index = (ino - 1) % inodes_per_group;
	desc_off = gdt_start_block * (u64)block_size + (u64)group * desc_size;
	if (read_bytes(desc_off, desc, desc_size < sizeof(desc) ? desc_size : sizeof(desc)) < 0) {
		return -E_INVAL;
	}
	inode_table = rd64_lohi(desc + 8, desc + 40);
	inode_off = inode_table * (u64)block_size + (u64)index * inode_size;
	return read_bytes(inode_off, inode, inode_size);
}

static int extent_lookup_rec(const u8 *node, u32 lblock, u64 *pblock, u16 *run) {
	u16 magic = rd16(node);
	u16 entries = rd16(node + 2);
	u16 depth = rd16(node + 6);

	if (magic != EXT4_EXT_MAGIC) {
		return -E_INVAL;
	}
	if (depth == 0) {
		const u8 *best = NULL;

		for (u16 i = 0; i < entries; i++) {
			const u8 *ex = node + 12 + (size_t)i * 12;
			u32 ee_block = rd32(ex);
			u16 ee_len = rd16(ex + 4) & 0x7fff;

			if (lblock >= ee_block && lblock < ee_block + ee_len) {
				best = ex;
				break;
			}
		}
		if (best == NULL) {
			return -E_INVAL;
		}
		*pblock = ((u64)rd16(best + 6) << 32) | rd32(best + 8);
		*pblock += lblock - rd32(best);
		*run = (rd16(best + 4) & 0x7fff) - (u16)(lblock - rd32(best));
		return 0;
	}

	{
		const u8 *best = NULL;
		static u8 child[4096];

		for (u16 i = 0; i < entries; i++) {
			const u8 *idx = node + 12 + (size_t)i * 12;
			u32 ei_block = rd32(idx);

			if (ei_block <= lblock) {
				best = idx;
			} else {
				break;
			}
		}
		if (best == NULL || block_size > sizeof(child)) {
			return -E_INVAL;
		}
		if (read_block(((u64)rd16(best + 8) << 32) | rd32(best + 4), child) < 0) {
			return -E_INVAL;
		}
		return extent_lookup_rec(child, lblock, pblock, run);
	}
}

static int inode_block_lookup(const u8 *inode, u32 lblock, u64 *pblock) {
	u16 run;

	return extent_lookup_rec(inode + EXT4_I_BLOCK_OFF, lblock, pblock, &run);
}

static long inode_read_at(u32 ino, size_t *pos, void *buf, size_t count) {
	static u8 block_buf[4096];
	u8 inode[256];
	u64 size;
	size_t done = 0;

	if (read_inode(ino, inode) < 0) {
		return -E_INVAL;
	}
	if (!inode_is_reg(inode) && !inode_is_dir(inode)) {
		return -E_INVAL;
	}
	if (block_size > sizeof(block_buf)) {
		return -E_INVAL;
	}
	size = inode_size_bytes(inode);
	if (*pos >= size) {
		return 0;
	}
	if (count > size - *pos) {
		count = (size_t)(size - *pos);
	}
	while (done < count) {
		u32 lblock = (u32)((*pos + done) / block_size);
		size_t off = (*pos + done) % block_size;
		size_t n = block_size - off;
		u64 pblock;

		if (n > count - done) {
			n = count - done;
		}
		if (inode_block_lookup(inode, lblock, &pblock) < 0) {
			memset((u8 *)buf + done, 0, n);
		} else if (read_block(pblock, block_buf) < 0) {
			return -E_INVAL;
		} else {
			memcpy((u8 *)buf + done, block_buf + off, n);
		}
		done += n;
	}
	*pos += done;
	return (long)done;
}

static void file_cache_insert(u32 ino, const char *name) {
	struct Ext4Stat st;
	size_t pos = 0;

	if (name == NULL || ext4_stat(ino, &st) < 0 || st.is_dir ||
	    st.size > EXT4_FILE_CACHE_SIZE) {
		return;
	}
	for (size_t i = 0; i < ARRAY_SIZE(file_cache); i++) {
		if (!file_cache[i].used || kstrcmp(file_cache[i].name, name) == 0) {
			long n = inode_read_at(ino, &pos, file_cache[i].data, st.size);

			if (n < 0 || (size_t)n != st.size) {
				return;
			}
			file_cache[i].used = 1;
			file_cache[i].size = st.size;
			copy_name(file_cache[i].name, name, kstrlen(name),
			          sizeof(file_cache[i].name));
			return;
		}
	}
}

static int lookup_child(u32 dir_ino, const char *name, u32 *child) {
	static u8 dir_buf[4096];
	size_t pos = 0;
	long n;

	if (cache_lookup(dir_ino, name, child) == 0) {
		return 0;
	}
	while ((n = inode_read_at(dir_ino, &pos, dir_buf, block_size)) > 0) {
		size_t off = 0;

		while (off + 8 <= (size_t)n) {
			u32 ino = rd32(dir_buf + off);
			u16 rec_len = rd16(dir_buf + off + 4);
			u8 name_len = dir_buf[off + 6];
			const char *entry_name = (const char *)(dir_buf + off + 8);

			if (rec_len < 8 || off + rec_len > (size_t)n) {
				break;
			}
			if (ino != 0) {
				cache_insert(dir_ino, ino, entry_name, name_len);
				if (name_eq(name, entry_name, name_len)) {
					*child = ino;
					return 0;
				}
			}
			off += rec_len;
		}
	}
	return -E_INVAL;
}

void ext4_init(void) {
	u8 super[1024];
	u32 log_block_size;
	struct Ext4Dirent warm_dirent;
	size_t warm_pos = 0;

	ext4_ready = 0;
	if (!block_available()) {
		printk("  ext4 skipped, no block device\n");
		return;
	}
	if (read_bytes(EXT4_SUPER_OFFSET, super, sizeof(super)) < 0) {
		printk("  ext4 superblock read failed\n");
		return;
	}
	if (rd16(super + 56) != EXT4_SUPER_MAGIC) {
		printk("  ext4 not found magic=%x\n", rd16(super + 56));
		return;
	}
	log_block_size = rd32(super + 24);
	block_size = 1024UL << log_block_size;
	blocks_per_group = rd32(super + 32);
	inodes_per_group = rd32(super + 40);
	inode_size = rd16(super + 88);
	desc_size = rd16(super + 254);
	if (block_size < 1024 || block_size > 4096 || blocks_per_group == 0 ||
	    inodes_per_group == 0 || inode_size < 128 || inode_size > 256) {
		printk("  ext4 unsupported geometry block=%u inode=%u\n", block_size, inode_size);
		return;
	}
	if (desc_size < 32) {
		desc_size = 32;
	}
	gdt_start_block = block_size == 1024 ? 2 : 1;
	ext4_ready = 1;
	memset(name_cache, 0, sizeof(name_cache));
	memset(file_cache, 0, sizeof(file_cache));
	printk("  ext4 ready block=%u inode=%u desc=%u\n", block_size, inode_size, desc_size);
	while (ext4_getdents(EXT4_ROOT_INO, &warm_pos, &warm_dirent) > 0) {
		if (!warm_dirent.is_dir) {
			file_cache_insert(warm_dirent.inode, warm_dirent.name);
		}
	}
}

int ext4_available(void) {
	return ext4_ready;
}

int ext4_lookup_path(const char *path, u32 *inode) {
	u32 cur = EXT4_ROOT_INO;
	const char *p = path;

	if (!ext4_ready || path == NULL || inode == NULL) {
		return -E_INVAL;
	}
	while (*p == '/') {
		p++;
	}
	while (p[0] == '.' && p[1] == '/') {
		p += 2;
	}
	if (*p == '\0' || kstrcmp(path, ".") == 0 || kstrcmp(path, "/") == 0) {
		*inode = EXT4_ROOT_INO;
		return 0;
	}
	while (*p != '\0') {
		char name[64];
		size_t len = 0;

		while (p[len] != '\0' && p[len] != '/') {
			len++;
		}
		if (len == 0) {
			p++;
			continue;
		}
		if (len >= sizeof(name)) {
			return -E_INVAL;
		}
		copy_name(name, p, len, sizeof(name));
		if (lookup_child(cur, name, &cur) < 0) {
			return -E_INVAL;
		}
		p += len;
		while (*p == '/') {
			p++;
		}
	}
	*inode = cur;
	return 0;
}

int ext4_stat(u32 ino, struct Ext4Stat *st) {
	u8 inode[256];

	if (st == NULL || read_inode(ino, inode) < 0) {
		return -E_INVAL;
	}
	st->is_dir = inode_is_dir(inode);
	st->size = (size_t)inode_size_bytes(inode);
	st->nlink = rd16(inode + 26);
	return 0;
}

long ext4_read(u32 ino, size_t *pos, void *buf, size_t count) {
	if (!ext4_ready || pos == NULL || buf == NULL) {
		return -E_INVAL;
	}
	return inode_read_at(ino, pos, buf, count);
}

long ext4_getdents(u32 ino, size_t *pos, struct Ext4Dirent *dirent) {
	static u8 dir_buf[4096];

	if (!ext4_ready || pos == NULL || dirent == NULL || block_size > sizeof(dir_buf)) {
		return -E_INVAL;
	}
	for (;;) {
		size_t block_off = *pos % block_size;
		size_t block_start = *pos - block_off;
		size_t read_pos = block_start;
		long n = inode_read_at(ino, &read_pos, dir_buf, block_size);

		if (n <= 0) {
			return 0;
		}
		while (block_off + 8 <= (size_t)n) {
			u32 entry_ino = rd32(dir_buf + block_off);
			u16 rec_len = rd16(dir_buf + block_off + 4);
			u8 name_len = dir_buf[block_off + 6];
			u8 file_type = dir_buf[block_off + 7];

			if (rec_len < 8 || block_off + rec_len > (size_t)n) {
				return 0;
			}
			*pos = block_start + block_off + rec_len;
			if (entry_ino != 0 && name_len > 0) {
				dirent->inode = entry_ino;
				dirent->is_dir = file_type == 2;
				copy_name(dirent->name, (char *)dir_buf + block_off + 8, name_len,
				          sizeof(dirent->name));
				return 1;
			}
			block_off += rec_len;
		}
		*pos = block_start + block_size;
	}
}

int ext4_read_all(const char *path, const void **image, size_t *size) {
	u32 ino;
	struct Ext4Stat st;
	size_t pos = 0;
	long n;

	if (image == NULL || size == NULL) {
		return -E_INVAL;
	}
	if (file_cache_lookup(path, image, size) == 0) {
		return 0;
	}
	if (ext4_lookup_path(path, &ino) < 0) {
		return -E_INVAL;
	}
	if (ext4_stat(ino, &st) < 0 || st.is_dir || st.size > sizeof(ext4_file_buf)) {
		return -E_INVAL;
	}
	n = ext4_read(ino, &pos, ext4_file_buf, st.size);
	if (n < 0 || (size_t)n != st.size) {
		return -E_INVAL;
	}
	*image = ext4_file_buf;
	*size = st.size;
	return 0;
}
