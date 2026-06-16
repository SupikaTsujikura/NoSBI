#ifndef _MOS_RISCV_PRINTK_H_
#define _MOS_RISCV_PRINTK_H_

#include <machine.h>
#include <stdarg.h>

void printk(const char *fmt, ...);
void panic_here(const char *file, int line, const char *func, const char *fmt, ...) __attribute__((noreturn));

#define panic(...) panic_here(__FILE__, __LINE__, __func__, __VA_ARGS__)

#define panic_on(expr)                                                                             \
	do {                                                                                         \
		int _r = (expr);                                                                     \
		if (_r != 0) {                                                                       \
			panic("'%s' returned %d", #expr, _r);                                        \
		}                                                                                    \
	} while (0)

#endif
