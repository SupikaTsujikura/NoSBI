#include <print.h>

static void print_char(fmt_callback_t, void *, char, int, int);
static void print_str(fmt_callback_t, void *, const char *, int, int);
static void print_num(fmt_callback_t, void *, unsigned long, int, int, int, int, char, int);

void vprintfmt(fmt_callback_t out, void *data, const char *fmt, va_list ap) {
	char c;
	const char *s;
	long num;
	int width;
	int long_flag;
	int neg_flag;
	int ladjust;
	char padc;

	for (;;) {
		int length = 0;
		s = fmt;
		for (; *fmt != '\0'; fmt++) {
			if (*fmt != '%') {
				length++;
			} else {
				out(data, s, length);
				length = 0;
				fmt++;
				break;
			}
		}

		out(data, s, length);
		if (!*fmt) {
			break;
		}

		ladjust = 0;
		padc = ' ';
		if (*fmt == '-') {
			ladjust = 1;
			fmt++;
		} else if (*fmt == '0') {
			padc = '0';
			fmt++;
		}

		width = 0;
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt) - '0';
			fmt++;
		}

		long_flag = 0;
		while (*fmt == 'l') {
			long_flag = 1;
			fmt++;
		}

		neg_flag = 0;
		switch (*fmt) {
		case 'b':
			num = long_flag ? va_arg(ap, long int) : va_arg(ap, int);
			print_num(out, data, num, 2, 0, width, ladjust, padc, 0);
			break;
		case 'd':
		case 'D':
			num = long_flag ? va_arg(ap, long int) : va_arg(ap, int);
			neg_flag = num < 0;
			num = neg_flag ? -num : num;
			print_num(out, data, num, 10, neg_flag, width, ladjust, padc, 0);
			break;
		case 'o':
		case 'O':
			num = long_flag ? va_arg(ap, long int) : va_arg(ap, int);
			print_num(out, data, num, 8, 0, width, ladjust, padc, 0);
			break;
		case 'u':
		case 'U':
			num = long_flag ? va_arg(ap, long int) : va_arg(ap, int);
			print_num(out, data, num, 10, 0, width, ladjust, padc, 0);
			break;
		case 'x':
			num = long_flag ? va_arg(ap, long int) : va_arg(ap, int);
			print_num(out, data, num, 16, 0, width, ladjust, padc, 0);
			break;
		case 'X':
			num = long_flag ? va_arg(ap, long int) : va_arg(ap, int);
			print_num(out, data, num, 16, 0, width, ladjust, padc, 1);
			break;
		case 'p':
			out(data, "0x", 2);
			num = (long)va_arg(ap, void *);
			print_num(out, data, num, 16, 0, 16, 0, '0', 0);
			break;
		case 'c':
			c = (char)va_arg(ap, int);
			print_char(out, data, c, width, ladjust);
			break;
		case 's':
			s = (char *)va_arg(ap, char *);
			if (s == NULL) {
				s = "(null)";
			}
			print_str(out, data, s, width, ladjust);
			break;
		case '%':
			out(data, "%", 1);
			break;
		case '\0':
			fmt--;
			break;
		default:
			out(data, fmt, 1);
		}
		fmt++;
	}
}

static void print_char(fmt_callback_t out, void *data, char c, int length, int ladjust) {
	int i;

	if (length < 1) {
		length = 1;
	}
	if (ladjust) {
		out(data, &c, 1);
		for (i = 1; i < length; i++) {
			out(data, " ", 1);
		}
	} else {
		for (i = 0; i < length - 1; i++) {
			out(data, " ", 1);
		}
		out(data, &c, 1);
	}
}

static void print_str(fmt_callback_t out, void *data, const char *s, int length, int ladjust) {
	int i;
	int len = 0;
	const char *s1 = s;
	while (*s1++) {
		len++;
	}
	if (length < len) {
		length = len;
	}

	if (ladjust) {
		out(data, s, len);
		for (i = len; i < length; i++) {
			out(data, " ", 1);
		}
	} else {
		for (i = 0; i < length - len; i++) {
			out(data, " ", 1);
		}
		out(data, s, len);
	}
}

static void print_num(fmt_callback_t out, void *data, unsigned long u, int base, int neg_flag,
			 int length, int ladjust, char padc, int upcase) {
	int actual_length = 0;
	char buf[length + 70];
	char *p = buf;
	int i;

	do {
		int tmp = u % base;
		if (tmp <= 9) {
			*p++ = '0' + tmp;
		} else if (upcase) {
			*p++ = 'A' + tmp - 10;
		} else {
			*p++ = 'a' + tmp - 10;
		}
		u /= base;
	} while (u != 0);

	if (neg_flag) {
		*p++ = '-';
	}

	actual_length = p - buf;
	if (length < actual_length) {
		length = actual_length;
	}

	if (ladjust) {
		padc = ' ';
	}
	if (neg_flag && !ladjust && (padc == '0')) {
		for (i = actual_length - 1; i < length - 1; i++) {
			buf[i] = padc;
		}
		buf[length - 1] = '-';
	} else {
		for (i = actual_length; i < length; i++) {
			buf[i] = padc;
		}
	}

	if (ladjust) {
		for (i = 0; i < actual_length; i++) {
			out(data, &buf[actual_length - 1 - i], 1);
		}
		for (; i < length; i++) {
			out(data, &buf[i], 1);
		}
	} else {
		for (i = length - 1; i >= 0; i--) {
			out(data, &buf[i], 1);
		}
	}
}
