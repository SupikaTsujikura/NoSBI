#include <print.h>
#include <printk.h>

void outputk(void *data, const char *buf, size_t len) {
	(void)data;
	for (size_t i = 0; i < len; i++) {
		printcharc(buf[i]);
	}
}

void printk(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintfmt(outputk, NULL, fmt, ap);
	va_end(ap);
}
