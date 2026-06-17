#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(const char *path, long *size_out) {
	FILE *fp = fopen(path, "rb");
	if (fp == NULL) {
		perror(path);
		return NULL;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		perror("fseek");
		fclose(fp);
		return NULL;
	}
	long size = ftell(fp);
	if (size < 0) {
		perror("ftell");
		fclose(fp);
		return NULL;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		perror("fseek");
		fclose(fp);
		return NULL;
	}
	char *buf = malloc((size_t)size + 1);
	if (buf == NULL) {
		fprintf(stderr, "malloc failed\n");
		fclose(fp);
		return NULL;
	}
	size_t got = fread(buf, 1, (size_t)size, fp);
	if (got != (size_t)size) {
		fprintf(stderr, "short read from %s\n", path);
		free(buf);
		fclose(fp);
		return NULL;
	}
	buf[size] = '\0';
	fclose(fp);
	if (size_out) {
		*size_out = size;
	}
	return buf;
}

static int require_in_order(const char *haystack, const char **needles, size_t count) {
	const char *cursor = haystack;
	for (size_t i = 0; i < count; i++) {
		const char *hit = strstr(cursor, needles[i]);
		if (hit == NULL) {
			fprintf(stderr, "missing required text: %s\n", needles[i]);
			return 0;
		}
		cursor = hit + strlen(needles[i]);
	}
	return 1;
}

static int count_char(const char *s, char ch) {
	int n = 0;
	for (; *s; s++) {
		if (*s == ch) {
			n++;
		}
	}
	return n;
}

static char *sanitize_log(char *raw, long size) {
	char *clean = malloc((size_t)size + 1);
	if (clean == NULL) {
		fprintf(stderr, "malloc failed\n");
		return NULL;
	}
	long w = 0;
	for (long r = 0; r < size; r++) {
		unsigned char ch = (unsigned char)raw[r];
		if (ch == '\r') {
			continue;
		}
		if (ch == '\0') {
			clean[w++] = '?';
			continue;
		}
		clean[w++] = (char)ch;
	}
	clean[w] = '\0';
	return clean;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <qemu-log>\n", argv[0]);
		return 2;
	}

	long size = 0;
	char *raw = read_all(argv[1], &size);
	if (raw == NULL) {
		return 2;
	}
	char *log = sanitize_log(raw, size);
	free(raw);
	if (log == NULL) {
		return 2;
	}

	const char *required[] = {
		"MOS/RISC-V bootstrap",
		"vm self-test passed",
		"satp enabled, kernel still alive",
		"env created: id=0x800 pri=1 name=user-a entry=0x400000",
		"env created: id=0x1001 pri=1 name=user-b entry=0x400000",
		"entering first user env via scheduler...",
		"schedule -> env=0x800 runs=1 name=user-a",
		"A",
		"schedule -> env=0x1001 runs=1 name=user-b",
		"B",
		"timer interrupt #1",
	};

	if (!require_in_order(log, required, sizeof(required) / sizeof(required[0]))) {
		free(log);
		return 1;
	}

	int a_count = count_char(log, 'A');
	int b_count = count_char(log, 'B');
	if (a_count < 2 || b_count < 2) {
		fprintf(stderr, "expected repeated user output, got A=%d B=%d\n", a_count, b_count);
		free(log);
		return 1;
	}

	printf("PASS: validated %ld bytes of QEMU output (A=%d, B=%d)\n", size, a_count, b_count);
	free(log);
	return 0;
}
