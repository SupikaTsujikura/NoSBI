#include <block.h>
#include <ext4.h>
#include <printk.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *disk;

int block_available(void) {
	return disk != NULL;
}

int block_read_sector(u64 sector, void *buf) {
	if (fseek(disk, (long)(sector * BLOCK_SECTOR_SIZE), SEEK_SET) != 0) {
		return -1;
	}
	return fread(buf, 1, BLOCK_SECTOR_SIZE, disk) == BLOCK_SECTOR_SIZE ? 0 : -1;
}

int block_write_sector(u64 sector, const void *buf) {
	if (fseek(disk, (long)(sector * BLOCK_SECTOR_SIZE), SEEK_SET) != 0) {
		return -1;
	}
	if (fwrite(buf, 1, BLOCK_SECTOR_SIZE, disk) != BLOCK_SECTOR_SIZE) {
		return -1;
	}
	fflush(disk);
	return 0;
}

void printk(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void panic_here(const char *file, int line, const char *func, const char *fmt, ...) {
	va_list ap;

	fprintf(stderr, "%s:%d:%s: ", file, line, func);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(2);
}

int main(int argc, char **argv) {
	static const char payload[] = "persistent-ext4\n";
	char readback[sizeof(payload)] = {0};
	struct Ext4Stat st;
	u32 ino;
	size_t pos;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s image [keep]\n", argv[0]);
		return 2;
	}
	disk = fopen(argv[1], "r+b");
	if (disk == NULL) {
		perror("fopen");
		return 2;
	}
	ext4_init();
	if (argc == 3 && argv[2][0] == 'v') {
		if (!ext4_available() ||
		    ext4_lookup_path("/persist/renamed.txt", &ino) < 0 ||
		    ext4_stat(ino, &st) < 0 || st.mode != 0600) {
			return 9;
		}
		pos = 0;
		if (ext4_read(ino, &pos, readback, sizeof(payload) - 1) !=
		    (long)(sizeof(payload) - 1)) {
			return 10;
		}
		if (ext4_unlink("/persist/renamed.txt") < 0 ||
		    ext4_unlink("/persist") < 0) {
			return 11;
		}
		fclose(disk);
		return 0;
	}
	if (!ext4_available() ||
	    ext4_mkdir("/persist", 0755, NULL) < 0 ||
	    ext4_create("/persist/file.txt", 0644, &ino) < 0) {
		return 3;
	}
	pos = 0;
	if (ext4_write(ino, &pos, payload, sizeof(payload) - 1) !=
	    (long)(sizeof(payload) - 1)) {
		return 4;
	}
	if (ext4_chmod(ino, 0600) < 0 ||
	    ext4_rename("/persist/file.txt", "/persist/renamed.txt") < 0 ||
	    ext4_lookup_path("/persist/renamed.txt", &ino) < 0 ||
	    ext4_stat(ino, &st) < 0 || st.mode != 0600) {
		return 5;
	}
	pos = 0;
	if (ext4_read(ino, &pos, readback, sizeof(payload) - 1) !=
	    (long)(sizeof(payload) - 1)) {
		return 6;
	}
	for (size_t i = 0; i < sizeof(payload) - 1; i++) {
		if (readback[i] != payload[i]) {
			return 7;
		}
	}
	if (argc == 2) {
		if (ext4_unlink("/persist/renamed.txt") < 0 ||
		    ext4_unlink("/persist") < 0) {
			return 8;
		}
	}
	fclose(disk);
	return 0;
}
