#include <stdarg.h>
#include <print.h>

#include "lib.h"

struct FdOut {
	int fd;
	int count;
};

static void cons_out(void *data, const char *buf, size_t len) {
	int *count = data;

	(void)syscall_print_cons(buf, len);
	*count += (int)len;
}

static void fd_out(void *data, const char *buf, size_t len) {
	struct FdOut *out = data;
	int r = write(out->fd, buf, len);

	if (r > 0) {
		out->count += r;
	}
}

int printf(const char *fmt, ...) {
	va_list ap;
	int count = 0;

	va_start(ap, fmt);
	vprintfmt(cons_out, &count, fmt, ap);
	va_end(ap);
	return count;
}

int fprintf(int fd, const char *fmt, ...) {
	va_list ap;
	struct FdOut out = {fd, 0};

	va_start(ap, fmt);
	vprintfmt(fd_out, &out, fmt, ap);
	va_end(ap);
	return out.count;
}

int debugf(const char *fmt, ...) {
	va_list ap;
	int count = 0;

	va_start(ap, fmt);
	vprintfmt(cons_out, &count, fmt, ap);
	va_end(ap);
	return count;
}
