#include <block.h>
#include <error.h>
#include <ext4.h>
#include <fs.h>
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
#define EXT4_FT_REG_FILE       1
#define EXT4_FT_DIR            2
#define EXT4_EXTENTS_FL        0x00080000
#define EXT4_FEATURE_RO_COMPAT_METADATA_CSUM 0x0400
#define EXT4_BG_INODE_UNINIT   0x0001
#define EXT4_BG_BLOCK_UNINIT   0x0002
#define EXT4_DIRENT_TAIL_SIZE  12

static int ext4_ready;
static u32 block_size;
static u32 blocks_per_group;
static u32 inodes_per_group;
static u32 total_inodes;
static u64 total_blocks;
static u32 first_data_block;
static u32 first_inode;
static u32 group_count;
static u32 feature_incompat;
static u32 feature_ro_compat;
static u32 checksum_seed;
static u16 inode_size;
static u16 desc_size;
static u64 gdt_start_block;
static u8 super_copy[1024];
static u8 ext4_file_buf[EXT4_MAX_FILE];

struct Ext4NameCache {
	int used;
	u32 parent;
	u32 ino;
	char name[64];
};

struct Ext4FileCache {
	int used;
	u32 ino;
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

static void wr16(u8 *p, u16 value) {
	p[0] = (u8)value;
	p[1] = (u8)(value >> 8);
}

static void wr32(u8 *p, u32 value) {
	p[0] = (u8)value;
	p[1] = (u8)(value >> 8);
	p[2] = (u8)(value >> 16);
	p[3] = (u8)(value >> 24);
}

static u32 crc32c(u32 crc, const void *data, size_t len) {
	const u8 *p = data;

	while (len-- != 0) {
		crc ^= *p++;
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78U : 0);
		}
	}
	return crc;
}

static u64 rd64_lohi(const u8 *lo, const u8 *hi) {
	return (u64)rd32(lo) | ((u64)rd32(hi) << 32);
}

static u32 group_desc_u32(const u8 *desc, size_t lo_off, size_t hi_off) {
	u32 value = rd16(desc + lo_off);

	if (desc_size >= 64) {
		value |= (u32)rd16(desc + hi_off) << 16;
	}
	return value;
}

static void group_desc_set_u32(u8 *desc, size_t lo_off, size_t hi_off, u32 value) {
	wr16(desc + lo_off, (u16)value);
	if (desc_size >= 64) {
		wr16(desc + hi_off, (u16)(value >> 16));
	}
}

static u64 group_desc_block(const u8 *desc, size_t lo_off, size_t hi_off) {
	u64 value = rd32(desc + lo_off);

	if (desc_size >= 64) {
		value |= (u64)rd32(desc + hi_off) << 32;
	}
	return value;
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

static int file_cache_read(u32 ino, size_t *pos, void *buf, size_t count) {
	for (size_t i = 0; i < ARRAY_SIZE(file_cache); i++) {
		size_t n;

		if (!file_cache[i].used || file_cache[i].ino != ino) {
			continue;
		}
		if (*pos >= file_cache[i].size) {
			return 0;
		}
		n = file_cache[i].size - *pos;
		if (n > count) {
			n = count;
		}
		memcpy(buf, file_cache[i].data + *pos, n);
		*pos += n;
		return (int)n;
	}
	return -E_INVAL;
}

static void file_cache_write(u32 ino, size_t pos, const void *buf, size_t count) {
	for (size_t i = 0; i < ARRAY_SIZE(file_cache); i++) {
		if (!file_cache[i].used || file_cache[i].ino != ino || pos >= file_cache[i].size) {
			continue;
		}
		if (count > file_cache[i].size - pos) {
			count = file_cache[i].size - pos;
		}
		memcpy(file_cache[i].data + pos, buf, count);
		return;
	}
}

static void file_cache_invalidate(u32 ino) {
	for (size_t i = 0; i < ARRAY_SIZE(file_cache); i++) {
		if (file_cache[i].used && file_cache[i].ino == ino) {
			memset(&file_cache[i], 0, sizeof(file_cache[i]));
		}
	}
}

static int read_sectors(u64 sector, void *buf, size_t count) {
	for (size_t i = 0; i < count; i++) {
		if (block_read_sector(sector + i, (u8 *)buf + i * BLOCK_SECTOR_SIZE) < 0) {
			return -E_INVAL;
		}
	}
	return 0;
}

static int write_sectors(u64 sector, const void *buf, size_t count) {
	for (size_t i = 0; i < count; i++) {
		if (block_write_sector(sector + i, (const u8 *)buf + i * BLOCK_SECTOR_SIZE) < 0) {
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

static int write_bytes(u64 offset, const void *buf, size_t size) {
	static u8 sector_buf[BLOCK_SECTOR_SIZE];
	size_t done = 0;

	while (done < size) {
		u64 sector = (offset + done) / BLOCK_SECTOR_SIZE;
		size_t off = (size_t)((offset + done) % BLOCK_SECTOR_SIZE);
		size_t n = BLOCK_SECTOR_SIZE - off;

		if (n > size - done) {
			n = size - done;
		}
		if ((off != 0 || n != BLOCK_SECTOR_SIZE) &&
		    block_read_sector(sector, sector_buf) < 0) {
			return -E_INVAL;
		}
		memcpy(sector_buf + off, (const u8 *)buf + done, n);
		if (block_write_sector(sector, sector_buf) < 0) {
			return -E_INVAL;
		}
		done += n;
	}
	return 0;
}

static int read_block(u64 block, void *buf) {
	return read_sectors(block * (block_size / BLOCK_SECTOR_SIZE), buf,
	                    block_size / BLOCK_SECTOR_SIZE);
}

static int write_block(u64 block, const void *buf) {
	return write_sectors(block * (block_size / BLOCK_SECTOR_SIZE), buf,
	                     block_size / BLOCK_SECTOR_SIZE);
}

static u64 group_desc_offset(u32 group) {
	return gdt_start_block * (u64)block_size + (u64)group * desc_size;
}

static int read_group_desc(u32 group, u8 desc[64]) {
	if (group >= group_count) {
		return -E_INVAL;
	}
	memset(desc, 0, 64);
	return read_bytes(group_desc_offset(group), desc,
	                  desc_size < 64 ? desc_size : 64);
}

static u16 group_desc_checksum(u32 group, const u8 *desc) {
	u8 copy[64];
	u8 group_le[4];
	u32 crc;

	memcpy(copy, desc, desc_size);
	wr16(copy + 30, 0);
	wr32(group_le, group);
	crc = crc32c(checksum_seed, group_le, sizeof(group_le));
	crc = crc32c(crc, copy, desc_size);
	return (u16)crc;
}

static int write_group_desc(u32 group, u8 desc[64]) {
	if (feature_ro_compat & EXT4_FEATURE_RO_COMPAT_METADATA_CSUM) {
		wr16(desc + 30, group_desc_checksum(group, desc));
	}
	return write_bytes(group_desc_offset(group), desc, desc_size);
}

static void update_super_checksum(void) {
	if (feature_ro_compat & EXT4_FEATURE_RO_COMPAT_METADATA_CSUM) {
		wr32(super_copy + 1020, crc32c(0xffffffffU, super_copy, 1020));
	}
}

static int write_super(void) {
	update_super_checksum();
	return write_bytes(EXT4_SUPER_OFFSET, super_copy, sizeof(super_copy));
}

static u32 bitmap_checksum(const void *bitmap, size_t bytes) {
	return crc32c(checksum_seed, bitmap, bytes);
}

static void group_desc_set_bitmap_checksum(u8 *desc, int inode_bitmap, u32 checksum) {
	size_t lo = inode_bitmap ? 26 : 24;
	size_t hi = inode_bitmap ? 58 : 56;

	wr16(desc + lo, (u16)checksum);
	if (desc_size >= 64) {
		wr16(desc + hi, (u16)(checksum >> 16));
	}
}

static u32 inode_checksum_seed(u32 ino, u32 generation) {
	u8 value[4];
	u32 crc;

	wr32(value, ino);
	crc = crc32c(checksum_seed, value, sizeof(value));
	wr32(value, generation);
	return crc32c(crc, value, sizeof(value));
}

static void inode_set_checksum(u32 ino, u8 *inode) {
	u16 extra = inode_size >= 130 ? rd16(inode + 128) : 0;
	u32 generation = rd32(inode + 100);
	u32 crc;

	if (!(feature_ro_compat & EXT4_FEATURE_RO_COMPAT_METADATA_CSUM)) {
		return;
	}
	wr16(inode + 124, 0);
	if (inode_size > 130 && extra >= 4) {
		wr16(inode + 130, 0);
	}
	crc = crc32c(inode_checksum_seed(ino, generation), inode, inode_size);
	wr16(inode + 124, (u16)crc);
	if (inode_size > 130 && extra >= 4) {
		wr16(inode + 130, (u16)(crc >> 16));
	}
}

static void dir_block_set_checksum(u32 ino, const u8 *inode, u8 *block) {
	u32 crc;

	if (!(feature_ro_compat & EXT4_FEATURE_RO_COMPAT_METADATA_CSUM) ||
	    block_size < EXT4_DIRENT_TAIL_SIZE ||
	    block[block_size - 5] != 0xde) {
		return;
	}
	crc = crc32c(inode_checksum_seed(ino, rd32(inode + 100)), block,
	             block_size - EXT4_DIRENT_TAIL_SIZE);
	wr32(block + block_size - 4, crc);
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

static int inode_disk_offset(u32 ino, u64 *offset) {
	u32 group;
	u32 index;
	u64 desc_off;
	u8 desc[64];
	u64 inode_table;
	u64 inode_off;

	if (!ext4_ready || ino == 0 || offset == NULL) {
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
	*offset = inode_off;
	return 0;
}

static int read_inode(u32 ino, u8 *inode) {
	u64 offset;

	if (inode == NULL || inode_disk_offset(ino, &offset) < 0) {
		return -E_INVAL;
	}
	return read_bytes(offset, inode, inode_size);
}

static int write_inode(u32 ino, const u8 *inode) {
	u8 copy[256];
	u64 offset;

	if (inode == NULL || inode_disk_offset(ino, &offset) < 0) {
		return -E_INVAL;
	}
	memcpy(copy, inode, inode_size);
	inode_set_checksum(ino, copy);
	return write_bytes(offset, copy, inode_size);
}

static void inode_set_size(u8 *inode, u64 size) {
	wr32(inode + 4, (u32)size);
	wr32(inode + 108, (u32)(size >> 32));
}

static int bitmap_test(const u8 *bitmap, u32 bit) {
	return (bitmap[bit >> 3] >> (bit & 7)) & 1;
}

static void bitmap_set(u8 *bitmap, u32 bit) {
	bitmap[bit >> 3] |= (u8)(1U << (bit & 7));
}

static void bitmap_clear(u8 *bitmap, u32 bit) {
	bitmap[bit >> 3] &= (u8)~(1U << (bit & 7));
}

static u32 blocks_in_group(u32 group) {
	u64 start = (u64)first_data_block + (u64)group * blocks_per_group;
	u64 remain = total_blocks > start ? total_blocks - start : 0;

	return remain > blocks_per_group ? blocks_per_group : (u32)remain;
}

static u32 inodes_in_group(u32 group) {
	u32 start = group * inodes_per_group;
	u32 remain = total_inodes > start ? total_inodes - start : 0;

	return remain > inodes_per_group ? inodes_per_group : remain;
}

static int update_allocation_counters(u32 group, int inode_alloc, int delta) {
	u8 desc[64];
	u32 group_free;
	u32 super_free;

	if (read_group_desc(group, desc) < 0) {
		return -E_INVAL;
	}
	if (inode_alloc) {
		group_free = group_desc_u32(desc, 14, 46);
		super_free = rd32(super_copy + 16);
	} else {
		group_free = group_desc_u32(desc, 12, 44);
		super_free = rd32(super_copy + 12);
	}
	if ((delta < 0 && (group_free == 0 || super_free == 0)) ||
	    (delta > 0 && (group_free == 0xffffffffU || super_free == 0xffffffffU))) {
		return -E_INVAL;
	}
	group_free = (u32)((int64_t)group_free + delta);
	super_free = (u32)((int64_t)super_free + delta);
	if (inode_alloc) {
		group_desc_set_u32(desc, 14, 46, group_free);
		wr32(super_copy + 16, super_free);
	} else {
		group_desc_set_u32(desc, 12, 44, group_free);
		wr32(super_copy + 12, super_free);
		if (total_blocks > 0xffffffffULL) {
			u64 full = rd64_lohi(super_copy + 12, super_copy + 0x158);
			full = (u64)((int64_t)full + delta);
			wr32(super_copy + 12, (u32)full);
			wr32(super_copy + 0x158, (u32)(full >> 32));
		}
	}
	if (write_group_desc(group, desc) < 0 || write_super() < 0) {
		return -E_INVAL;
	}
	return 0;
}

static int allocate_bitmap_entry(int inode_bitmap, u32 preferred_group, u64 *value) {
	static u8 bitmap[4096];

	for (u32 step = 0; step < group_count; step++) {
		u32 group = (preferred_group + step) % group_count;
		u8 desc[64];
		u64 bitmap_block;
		u32 count = inode_bitmap ? inodes_in_group(group) : blocks_in_group(group);
		size_t checksum_bytes = inode_bitmap ? (inodes_per_group + 7) / 8 : block_size;

		if (read_group_desc(group, desc) < 0 ||
		    (rd16(desc + 18) &
		     (inode_bitmap ? EXT4_BG_INODE_UNINIT : EXT4_BG_BLOCK_UNINIT))) {
			continue;
		}
		bitmap_block = inode_bitmap ? group_desc_block(desc, 4, 36) :
		                             group_desc_block(desc, 0, 32);
		if (block_size > sizeof(bitmap) || read_block(bitmap_block, bitmap) < 0) {
			return -E_INVAL;
		}
		for (u32 bit = 0; bit < count; bit++) {
			u64 result;

			if (bitmap_test(bitmap, bit)) {
				continue;
			}
			result = inode_bitmap ?
			         (u64)group * inodes_per_group + bit + 1 :
			         (u64)first_data_block + (u64)group * blocks_per_group + bit;
			if (inode_bitmap && result < first_inode) {
				continue;
			}
			bitmap_set(bitmap, bit);
			if (inode_bitmap) {
				u32 unused = group_desc_u32(desc, 28, 50);
				u32 initialized = count > unused ? count - unused : 0;

				if (bit >= initialized) {
					group_desc_set_u32(desc, 28, 50, count - bit - 1);
				}
			}
			group_desc_set_bitmap_checksum(
			    desc, inode_bitmap, bitmap_checksum(bitmap, checksum_bytes));
			if (write_block(bitmap_block, bitmap) < 0 ||
			    write_group_desc(group, desc) < 0 ||
			    update_allocation_counters(group, inode_bitmap, -1) < 0) {
				return -E_INVAL;
			}
			*value = result;
			return 0;
		}
	}
	return -E_NO_MEM;
}

static int free_bitmap_entry(int inode_bitmap, u64 value) {
	static u8 bitmap[4096];
	u64 relative;
	u32 group;
	u32 bit;
	u8 desc[64];
	u64 bitmap_block;
	size_t checksum_bytes;

	if ((inode_bitmap && (value == 0 || value > total_inodes)) ||
	    (!inode_bitmap && (value < first_data_block || value >= total_blocks))) {
		return -E_INVAL;
	}
	relative = inode_bitmap ? value - 1 : value - first_data_block;
	group = (u32)(relative / (inode_bitmap ? inodes_per_group : blocks_per_group));
	bit = (u32)(relative % (inode_bitmap ? inodes_per_group : blocks_per_group));
	if (read_group_desc(group, desc) < 0) {
		return -E_INVAL;
	}
	bitmap_block = inode_bitmap ? group_desc_block(desc, 4, 36) :
	                             group_desc_block(desc, 0, 32);
	checksum_bytes = inode_bitmap ? (inodes_per_group + 7) / 8 : block_size;
	if (read_block(bitmap_block, bitmap) < 0 || !bitmap_test(bitmap, bit)) {
		return -E_INVAL;
	}
	bitmap_clear(bitmap, bit);
	if (inode_bitmap) {
		u32 count = inodes_in_group(group);
		u32 highest = 0;
		int found = 0;

		for (u32 i = count; i > 0; i--) {
			if (bitmap_test(bitmap, i - 1)) {
				highest = i - 1;
				found = 1;
				break;
			}
		}
		group_desc_set_u32(desc, 28, 50,
		                   found ? count - highest - 1 : count);
	}
	group_desc_set_bitmap_checksum(
	    desc, inode_bitmap, bitmap_checksum(bitmap, checksum_bytes));
	if (write_block(bitmap_block, bitmap) < 0 ||
	    write_group_desc(group, desc) < 0 ||
	    update_allocation_counters(group, inode_bitmap, 1) < 0) {
		return -E_INVAL;
	}
	return 0;
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

static int inode_append_extent(u8 *inode, u32 lblock, u64 pblock, u16 count) {
	u8 *header = inode + EXT4_I_BLOCK_OFF;
	u16 entries = rd16(header + 2);
	u16 max = rd16(header + 4);
	u8 *extent;

	if (rd16(header) != EXT4_EXT_MAGIC || rd16(header + 6) != 0 ||
	    count == 0 || count > 0x7fff) {
		return -E_INVAL;
	}
	if (entries > 0) {
		u8 *last = header + 12 + (size_t)(entries - 1) * 12;
		u32 last_lblock = rd32(last);
		u16 last_len = rd16(last + 4) & 0x7fff;
		u64 last_pblock = ((u64)rd16(last + 6) << 32) | rd32(last + 8);

		if (last_lblock + last_len == lblock &&
		    last_pblock + last_len == pblock &&
		    last_len + count <= 0x7fff) {
			wr16(last + 4, (u16)(last_len + count));
			return 0;
		}
	}
	if (entries >= max || max == 0) {
		return -E_NO_MEM;
	}
	extent = header + 12 + (size_t)entries * 12;
	wr32(extent, lblock);
	wr16(extent + 4, count);
	wr16(extent + 6, (u16)(pblock >> 32));
	wr32(extent + 8, (u32)pblock);
	wr16(header + 2, entries + 1);
	return 0;
}

static void inode_init_extent(u8 *inode, u16 mode, u16 links) {
	u8 *header;

	memset(inode, 0, inode_size);
	wr16(inode, mode);
	wr16(inode + 26, links);
	wr32(inode + 32, EXT4_EXTENTS_FL);
	if (inode_size >= 160) {
		wr16(inode + 128, 32);
	}
	header = inode + EXT4_I_BLOCK_OFF;
	wr16(header, EXT4_EXT_MAGIC);
	wr16(header + 2, 0);
	wr16(header + 4, 4);
	wr16(header + 6, 0);
	wr32(header + 8, 0);
}

static u16 dirent_size(size_t name_len) {
	return (u16)((8 + name_len + 3) & ~3U);
}

static size_t dir_data_end(void) {
	return (feature_ro_compat & EXT4_FEATURE_RO_COMPAT_METADATA_CSUM) ?
	       block_size - EXT4_DIRENT_TAIL_SIZE : block_size;
}

static void dir_init_tail(u8 *block) {
	u8 *tail;

	if (!(feature_ro_compat & EXT4_FEATURE_RO_COMPAT_METADATA_CSUM)) {
		return;
	}
	tail = block + block_size - EXT4_DIRENT_TAIL_SIZE;
	memset(tail, 0, EXT4_DIRENT_TAIL_SIZE);
	wr16(tail + 4, EXT4_DIRENT_TAIL_SIZE);
	tail[7] = 0xde;
}

static void dir_write_entry(u8 *entry, u32 ino, u16 rec_len,
                            u8 file_type, const char *name, u8 name_len) {
	memset(entry, 0, rec_len);
	wr32(entry, ino);
	wr16(entry + 4, rec_len);
	entry[6] = name_len;
	entry[7] = file_type;
	memcpy(entry + 8, name, name_len);
}

static int split_path(const char *path, char parent[FS_PATH_MAX], char name[FS_PATH_MAX]) {
	size_t len;
	size_t slash;

	if (path == NULL || path[0] == '\0' || kstrcmp(path, "/") == 0) {
		return -E_INVAL;
	}
	len = kstrlen(path);
	while (len > 1 && path[len - 1] == '/') {
		len--;
	}
	slash = len;
	while (slash > 0 && path[slash - 1] != '/') {
		slash--;
	}
	if (slash == 0 || len - slash == 0 || len - slash >= FS_PATH_MAX) {
		return -E_INVAL;
	}
	if (slash == 1) {
		parent[0] = '/';
		parent[1] = '\0';
	} else {
		memcpy(parent, path, slash - 1);
		parent[slash - 1] = '\0';
	}
	memcpy(name, path + slash, len - slash);
	name[len - slash] = '\0';
	return 0;
}

static int dir_add_entry(u32 dir_ino, u32 child_ino, u8 file_type, const char *name) {
	static u8 block[4096];
	u8 dir_inode[256];
	u16 needed;
	size_t name_len = kstrlen(name);
	u32 blocks;

	if (name_len == 0 || name_len > 255 || block_size > sizeof(block) ||
	    read_inode(dir_ino, dir_inode) < 0 || !inode_is_dir(dir_inode)) {
		return -E_INVAL;
	}
	needed = dirent_size(name_len);
	blocks = (u32)((inode_size_bytes(dir_inode) + block_size - 1) / block_size);
	for (u32 lblock = 0; lblock < blocks; lblock++) {
		u64 pblock;
		size_t off = 0;
		size_t end = dir_data_end();

		if (inode_block_lookup(dir_inode, lblock, &pblock) < 0 ||
		    read_block(pblock, block) < 0) {
			continue;
		}
		while (off + 8 <= end) {
			u32 entry_ino = rd32(block + off);
			u16 rec_len = rd16(block + off + 4);
			u8 old_name_len = block[off + 6];
			u16 ideal = dirent_size(old_name_len);

			if (rec_len < 8 || off + rec_len > end) {
				break;
			}
			if (entry_ino == 0 && rec_len >= needed) {
				dir_write_entry(block + off, child_ino, rec_len,
				                file_type, name, (u8)name_len);
				dir_block_set_checksum(dir_ino, dir_inode, block);
				memset(name_cache, 0, sizeof(name_cache));
				return write_block(pblock, block);
			}
			if (entry_ino != 0 && rec_len >= ideal + needed) {
				wr16(block + off + 4, ideal);
				dir_write_entry(block + off + ideal, child_ino,
				                rec_len - ideal, file_type, name, (u8)name_len);
				dir_block_set_checksum(dir_ino, dir_inode, block);
				memset(name_cache, 0, sizeof(name_cache));
				return write_block(pblock, block);
			}
			off += rec_len;
		}
	}
	{
		u64 pblock;
		u32 lblock = blocks;

		if (allocate_bitmap_entry(0, (dir_ino - 1) / inodes_per_group, &pblock) < 0) {
			return -E_NO_MEM;
		}
		if (inode_append_extent(dir_inode, lblock, pblock, 1) < 0) {
			(void)free_bitmap_entry(0, pblock);
			return -E_NO_MEM;
		}
		memset(block, 0, block_size);
		dir_write_entry(block, child_ino, (u16)dir_data_end(),
		                file_type, name, (u8)name_len);
		dir_init_tail(block);
		dir_block_set_checksum(dir_ino, dir_inode, block);
		if (write_block(pblock, block) < 0) {
			(void)free_bitmap_entry(0, pblock);
			return -E_INVAL;
		}
		inode_set_size(dir_inode, (u64)(lblock + 1) * block_size);
		wr32(dir_inode + 28, rd32(dir_inode + 28) +
		     (u32)(block_size / BLOCK_SECTOR_SIZE));
		memset(name_cache, 0, sizeof(name_cache));
		return write_inode(dir_ino, dir_inode);
	}
}

static int dir_remove_entry(u32 dir_ino, const char *name,
                            u32 *removed_ino, u8 *removed_type) {
	static u8 block[4096];
	u8 dir_inode[256];
	size_t name_len = kstrlen(name);
	u32 blocks;

	if (read_inode(dir_ino, dir_inode) < 0 || !inode_is_dir(dir_inode) ||
	    block_size > sizeof(block)) {
		return -E_INVAL;
	}
	blocks = (u32)((inode_size_bytes(dir_inode) + block_size - 1) / block_size);
	for (u32 lblock = 0; lblock < blocks; lblock++) {
		u64 pblock;
		size_t off = 0;
		size_t prev = (size_t)-1;
		size_t end = dir_data_end();

		if (inode_block_lookup(dir_inode, lblock, &pblock) < 0 ||
		    read_block(pblock, block) < 0) {
			continue;
		}
		while (off + 8 <= end) {
			u32 entry_ino = rd32(block + off);
			u16 rec_len = rd16(block + off + 4);
			u8 entry_name_len = block[off + 6];

			if (rec_len < 8 || off + rec_len > end) {
				break;
			}
			if (entry_ino != 0 && entry_name_len == name_len &&
			    name_eq(name, (const char *)block + off + 8, name_len)) {
				if (removed_ino != NULL) {
					*removed_ino = entry_ino;
				}
				if (removed_type != NULL) {
					*removed_type = block[off + 7];
				}
				if (prev != (size_t)-1) {
					wr16(block + prev + 4,
					     rd16(block + prev + 4) + rec_len);
				} else {
					wr32(block + off, 0);
				}
				dir_block_set_checksum(dir_ino, dir_inode, block);
				memset(name_cache, 0, sizeof(name_cache));
				return write_block(pblock, block);
			}
			if (entry_ino != 0) {
				prev = off;
			}
			off += rec_len;
		}
	}
	return -E_INVAL;
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
			file_cache[i].ino = ino;
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
	memcpy(super_copy, super, sizeof(super_copy));
	log_block_size = rd32(super + 24);
	block_size = 1024UL << log_block_size;
	total_inodes = rd32(super + 0);
	total_blocks = rd64_lohi(super + 4, super + 0x150);
	first_data_block = rd32(super + 20);
	blocks_per_group = rd32(super + 32);
	inodes_per_group = rd32(super + 40);
	first_inode = rd32(super + 84);
	feature_incompat = rd32(super + 96);
	feature_ro_compat = rd32(super + 100);
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
	if (desc_size > 64) {
		printk("  ext4 unsupported descriptor size=%u\n", desc_size);
		return;
	}
	group_count = (u32)((total_blocks - first_data_block + blocks_per_group - 1) /
	                    blocks_per_group);
	checksum_seed = (feature_incompat & 0x2000) ?
	                rd32(super + 0x270) :
	                crc32c(0xffffffffU, super + 104, 16);
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
	st->mode = rd16(inode) & 0777;
	st->uid = rd16(inode + 2) | ((u32)rd16(inode + 120) << 16);
	st->gid = rd16(inode + 24) | ((u32)rd16(inode + 122) << 16);
	return 0;
}

long ext4_read(u32 ino, size_t *pos, void *buf, size_t count) {
	long r;

	if (!ext4_ready || pos == NULL || buf == NULL) {
		return -E_INVAL;
	}
	r = file_cache_read(ino, pos, buf, count);
	if (r >= 0) {
		return r;
	}
	return inode_read_at(ino, pos, buf, count);
}

long ext4_write(u32 ino, size_t *pos, const void *buf, size_t count) {
	static u8 block_buf[4096];
	u8 inode[256];
	u64 size;
	size_t done = 0;
	int inode_dirty = 0;

	if (!ext4_ready || pos == NULL || buf == NULL) {
		return -E_INVAL;
	}
	if (read_inode(ino, inode) < 0 || !inode_is_reg(inode)) {
		return -E_INVAL;
	}
	if (block_size > sizeof(block_buf)) {
		return -E_INVAL;
	}
	size = inode_size_bytes(inode);
	if (*pos > size || *pos + count < *pos) {
		return -E_INVAL;
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
			if (allocate_bitmap_entry(0, (ino - 1) / inodes_per_group,
			                          &pblock) < 0) {
				if (done == 0) {
					return -E_NO_MEM;
				}
				break;
			}
			if (inode_append_extent(inode, lblock, pblock, 1) < 0) {
				(void)free_bitmap_entry(0, pblock);
				if (done == 0) {
					return -E_NO_MEM;
				}
				break;
			}
			memset(block_buf, 0, block_size);
			if (write_block(pblock, block_buf) < 0) {
				return done == 0 ? -E_INVAL : (long)done;
			}
			wr32(inode + 28, rd32(inode + 28) +
			     (u32)(block_size / BLOCK_SECTOR_SIZE));
			inode_dirty = 1;
		}
		if (n == block_size) {
			if (write_block(pblock, (const u8 *)buf + done) < 0) {
				return -E_INVAL;
			}
		} else {
			if (read_block(pblock, block_buf) < 0) {
				return -E_INVAL;
			}
			memcpy(block_buf + off, (const u8 *)buf + done, n);
			if (write_block(pblock, block_buf) < 0) {
				return -E_INVAL;
			}
		}
		file_cache_write(ino, *pos + done, (const u8 *)buf + done, n);
		done += n;
	}
	*pos += done;
	if (*pos > size) {
		inode_set_size(inode, *pos);
		inode_dirty = 1;
		file_cache_invalidate(ino);
	}
	if (inode_dirty && write_inode(ino, inode) < 0) {
		return -E_INVAL;
	}
	return (long)done;
}

int ext4_truncate(u32 ino, size_t size) {
	u8 inode[256];
	u64 pblock;

	if (!ext4_ready || read_inode(ino, inode) < 0 || !inode_is_reg(inode)) {
		return -E_INVAL;
	}
	if (size > 0 &&
	    inode_block_lookup(inode, (u32)((size - 1) / block_size), &pblock) < 0) {
		return -E_NO_MEM;
	}
	inode_set_size(inode, size);
	if (write_inode(ino, inode) < 0) {
		return -E_INVAL;
	}
	file_cache_invalidate(ino);
	return 0;
}

int ext4_chmod(u32 ino, u32 mode) {
	u8 inode[256];
	u16 old_mode;

	if (!ext4_ready || read_inode(ino, inode) < 0) {
		return -E_INVAL;
	}
	old_mode = rd16(inode);
	wr16(inode, (old_mode & 0xf000) | (mode & 0777));
	return write_inode(ino, inode);
}

static int update_used_dirs(u32 group, int delta) {
	u8 desc[64];
	u32 used;

	if (read_group_desc(group, desc) < 0) {
		return -E_INVAL;
	}
	used = group_desc_u32(desc, 16, 48);
	used = (u32)((int64_t)used + delta);
	group_desc_set_u32(desc, 16, 48, used);
	return write_group_desc(group, desc);
}

static int inode_free_blocks(u8 *inode) {
	u8 *header = inode + EXT4_I_BLOCK_OFF;
	u16 entries;

	if (rd16(header) != EXT4_EXT_MAGIC || rd16(header + 6) != 0) {
		return inode_size_bytes(inode) == 0 ? 0 : -E_INVAL;
	}
	entries = rd16(header + 2);
	for (u16 i = 0; i < entries; i++) {
		u8 *extent = header + 12 + (size_t)i * 12;
		u16 len = rd16(extent + 4) & 0x7fff;
		u64 start = ((u64)rd16(extent + 6) << 32) | rd32(extent + 8);

		for (u16 j = 0; j < len; j++) {
			if (free_bitmap_entry(0, start + j) < 0) {
				return -E_INVAL;
			}
		}
	}
	return 0;
}

static int inode_blocks_releasable(const u8 *inode) {
	const u8 *header = inode + EXT4_I_BLOCK_OFF;

	return inode_size_bytes(inode) == 0 ||
	       (rd16(header) == EXT4_EXT_MAGIC && rd16(header + 6) == 0);
}

static int inode_release(u32 ino, u8 *inode) {
	if (inode_free_blocks(inode) < 0) {
		return -E_INVAL;
	}
	memset(inode, 0, inode_size);
	if (write_inode(ino, inode) < 0 || free_bitmap_entry(1, ino) < 0) {
		return -E_INVAL;
	}
	file_cache_invalidate(ino);
	return 0;
}

static int dir_is_empty(u32 ino) {
	struct Ext4Dirent entry;
	size_t pos = 0;

	while (ext4_getdents(ino, &pos, &entry) > 0) {
		if (kstrcmp(entry.name, ".") != 0 && kstrcmp(entry.name, "..") != 0) {
			return 0;
		}
	}
	return 1;
}

static int inode_adjust_links(u32 ino, int delta) {
	u8 inode[256];
	u16 links;

	if (read_inode(ino, inode) < 0) {
		return -E_INVAL;
	}
	links = rd16(inode + 26);
	if ((delta < 0 && links == 0) || (delta > 0 && links == 0xffff)) {
		return -E_INVAL;
	}
	wr16(inode + 26, (u16)((int)links + delta));
	return write_inode(ino, inode);
}

static int create_inode_at(const char *path, u16 mode, u8 file_type,
                           int directory, u32 *ino_store) {
	static u8 dir_block[4096];
	char parent_path[FS_PATH_MAX];
	char name[FS_PATH_MAX];
	u32 parent_ino;
	u64 allocated_ino;
	u64 allocated_block = 0;
	u8 inode[256];
	int r;

	if (split_path(path, parent_path, name) < 0 ||
	    ext4_lookup_path(path, &parent_ino) == 0 ||
	    ext4_lookup_path(parent_path, &parent_ino) < 0) {
		return -E_INVAL;
	}
	{
		u8 parent_inode[256];

		if (read_inode(parent_ino, parent_inode) < 0 ||
		    !inode_is_dir(parent_inode)) {
			return -E_INVAL;
		}
	}
	r = allocate_bitmap_entry(1, (parent_ino - 1) / inodes_per_group,
	                          &allocated_ino);
	if (r < 0) {
		return r;
	}
	inode_init_extent(inode, mode, directory ? 2 : 1);
	if (directory) {
		r = allocate_bitmap_entry(0, (parent_ino - 1) / inodes_per_group,
		                          &allocated_block);
		if (r < 0) {
			(void)free_bitmap_entry(1, allocated_ino);
			return r;
		}
		if (inode_append_extent(inode, 0, allocated_block, 1) < 0) {
			(void)free_bitmap_entry(0, allocated_block);
			(void)free_bitmap_entry(1, allocated_ino);
			return -E_INVAL;
		}
		inode_set_size(inode, block_size);
		wr32(inode + 28, (u32)(block_size / BLOCK_SECTOR_SIZE));
		memset(dir_block, 0, block_size);
		dir_write_entry(dir_block, (u32)allocated_ino, 12,
		                EXT4_FT_DIR, ".", 1);
		dir_write_entry(dir_block + 12, parent_ino,
		                (u16)(dir_data_end() - 12),
		                EXT4_FT_DIR, "..", 2);
		dir_init_tail(dir_block);
		dir_block_set_checksum((u32)allocated_ino, inode, dir_block);
		if (write_block(allocated_block, dir_block) < 0) {
			(void)free_bitmap_entry(0, allocated_block);
			(void)free_bitmap_entry(1, allocated_ino);
			return -E_INVAL;
		}
	}
	if (write_inode((u32)allocated_ino, inode) < 0) {
		if (directory) {
			(void)free_bitmap_entry(0, allocated_block);
		}
		(void)free_bitmap_entry(1, allocated_ino);
		return -E_INVAL;
	}
	r = dir_add_entry(parent_ino, (u32)allocated_ino, file_type, name);
	if (r < 0) {
		(void)inode_release((u32)allocated_ino, inode);
		return r;
	}
	if (directory) {
		(void)inode_adjust_links(parent_ino, 1);
		(void)update_used_dirs((u32)(allocated_ino - 1) / inodes_per_group, 1);
	}
	if (ino_store != NULL) {
		*ino_store = (u32)allocated_ino;
	}
	return 0;
}

int ext4_create(const char *path, u32 mode, u32 *ino) {
	return create_inode_at(path, EXT4_S_IFREG | (mode & 0777),
	                       EXT4_FT_REG_FILE, 0, ino);
}

int ext4_mkdir(const char *path, u32 mode, u32 *ino) {
	return create_inode_at(path, EXT4_S_IFDIR | (mode & 0777),
	                       EXT4_FT_DIR, 1, ino);
}

int ext4_unlink(const char *path) {
	char parent_path[FS_PATH_MAX];
	char name[FS_PATH_MAX];
	u32 parent_ino;
	u32 ino;
	u8 type;
	u8 inode[256];
	u16 links;

	if (split_path(path, parent_path, name) < 0 ||
	    ext4_lookup_path(parent_path, &parent_ino) < 0 ||
	    ext4_lookup_path(path, &ino) < 0 || ino == EXT4_ROOT_INO ||
	    read_inode(ino, inode) < 0) {
		return -E_INVAL;
	}
	if (inode_is_dir(inode) && !dir_is_empty(ino)) {
		return -E_INVAL;
	}
	links = rd16(inode + 26);
	if (links <= 1 && !inode_blocks_releasable(inode)) {
		return -E_INVAL;
	}
	if (dir_remove_entry(parent_ino, name, &ino, &type) < 0) {
		return -E_INVAL;
	}
	if (inode_is_dir(inode)) {
		(void)inode_adjust_links(parent_ino, -1);
		(void)update_used_dirs((ino - 1) / inodes_per_group, -1);
		wr16(inode + 26, 0);
		return inode_release(ino, inode);
	}
	if (links > 1) {
		wr16(inode + 26, links - 1);
		return write_inode(ino, inode);
	}
	wr16(inode + 26, 0);
	return inode_release(ino, inode);
}

int ext4_rename(const char *old_path, const char *new_path) {
	char old_parent_path[FS_PATH_MAX];
	char old_name[FS_PATH_MAX];
	char new_parent_path[FS_PATH_MAX];
	char new_name[FS_PATH_MAX];
	u32 old_parent;
	u32 new_parent;
	u32 ino;
	u8 inode[256];
	u8 type;

	if (split_path(old_path, old_parent_path, old_name) < 0 ||
	    split_path(new_path, new_parent_path, new_name) < 0 ||
	    ext4_lookup_path(old_parent_path, &old_parent) < 0 ||
	    ext4_lookup_path(new_parent_path, &new_parent) < 0 ||
	    ext4_lookup_path(old_path, &ino) < 0 ||
	    ext4_lookup_path(new_path, &ino) == 0 ||
	    read_inode(ino, inode) < 0) {
		return -E_INVAL;
	}
	type = inode_is_dir(inode) ? EXT4_FT_DIR : EXT4_FT_REG_FILE;
	if (dir_add_entry(new_parent, ino, type, new_name) < 0) {
		return -E_INVAL;
	}
	if (dir_remove_entry(old_parent, old_name, NULL, NULL) < 0) {
		(void)dir_remove_entry(new_parent, new_name, NULL, NULL);
		return -E_INVAL;
	}
	if (inode_is_dir(inode) && old_parent != new_parent) {
		static u8 block[4096];
		u64 pblock;

		if (inode_block_lookup(inode, 0, &pblock) < 0 ||
		    read_block(pblock, block) < 0) {
			return -E_INVAL;
		}
		wr32(block + rd16(block + 4), new_parent);
		dir_block_set_checksum(ino, inode, block);
		if (write_block(pblock, block) < 0) {
			return -E_INVAL;
		}
		(void)inode_adjust_links(old_parent, -1);
		(void)inode_adjust_links(new_parent, 1);
	}
	memset(name_cache, 0, sizeof(name_cache));
	return 0;
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
