#include <string.h>
#include <types.h>

void *memcpy(void *dst, const void *src, size_t n) {
	void *dstaddr = dst;
	void *max = dst + n;

	if (((u_long)src & 7) != ((u_long)dst & 7)) {
		while (dst < max) {
			*(char *)dst++ = *(const char *)src++;
		}
		return dstaddr;
	}

	while (((u_long)dst & 7) && dst < max) {
		*(char *)dst++ = *(const char *)src++;
	}

	while (dst + 8 <= max) {
		*(uint64_t *)dst = *(const uint64_t *)src;
		dst += 8;
		src += 8;
	}

	while (dst < max) {
		*(char *)dst++ = *(const char *)src++;
	}
	return dstaddr;
}

void *memset(void *dst, int c, size_t n) {
	void *dstaddr = dst;
	void *max = dst + n;
	u_char byte = c & 0xff;
	uint64_t word = byte;
	word |= word << 8;
	word |= word << 16;
	word |= word << 32;

	while (((u_long)dst & 7) && dst < max) {
		*(u_char *)dst++ = byte;
	}

	while (dst + 8 <= max) {
		*(uint64_t *)dst = word;
		dst += 8;
	}

	while (dst < max) {
		*(u_char *)dst++ = byte;
	}
	return dstaddr;
}

size_t strlen(const char *s) {
	size_t n;

	for (n = 0; *s; s++) {
		n++;
	}
	return n;
}

char *strcpy(char *dst, const char *src) {
	char *ret = dst;

	while ((*dst++ = *src++) != 0) {
	}
	return ret;
}

const char *strchr(const char *s, int c) {
	for (; *s; s++) {
		if (*s == c) {
			return s;
		}
	}
	return 0;
}

int strcmp(const char *p, const char *q) {
	while (*p && *p == *q) {
		p++, q++;
	}

	if ((u_int)*p < (u_int)*q) {
		return -1;
	}
	if ((u_int)*p > (u_int)*q) {
		return 1;
	}
	return 0;
}
