#include "lib.h"

static unsigned long wb_strlen(const char *s) {
	unsigned long n = 0;

	while (s[n] != '\0') {
		n++;
	}
	return n;
}

static void wb_puts(const char *s) {
	(void)syscall_print_cons(s, wb_strlen(s));
}

static void wb_putnum(int value) {
	char buf[16];
	int i = 0;
	int neg = value < 0;
	unsigned int x = neg ? (unsigned int)(-value) : (unsigned int)value;

	if (neg) {
		wb_puts("-");
	}
	do {
		buf[i++] = (char)('0' + x % 10);
		x /= 10;
	} while (x != 0 && i < (int)sizeof(buf));
	while (i > 0) {
		char ch = buf[--i];

		(void)syscall_print_cons(&ch, 1);
	}
}

void user_main(long arg, char **argv) {
	char buf[32];
	int fd;
	int n;

	(void)arg;
	(void)argv;
	fd = open("/disk.txt", FS_OPEN_CREATE);
	if (fd < 0) {
		wb_puts("[writeback-open-fail:");
		wb_putnum(fd);
		wb_puts("]");
		exit();
	}
	(void)seek(fd, 0);
	if (write(fd, "HELLO", 5) != 5) {
		wb_puts("[writeback-write-fail]");
		(void)close(fd);
		exit();
	}
	(void)seek(fd, 0);
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n > 0) {
		buf[n] = '\0';
		wb_puts("[writeback-read:");
		wb_puts(buf);
		wb_puts("]");
	}
	if (sync() == 0) {
		wb_puts("[writeback-sync-ok]");
	} else {
		wb_puts("[writeback-sync-fail]");
	}
	exit();
}
