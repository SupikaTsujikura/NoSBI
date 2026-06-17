#include "lib.h"

void user_main(long arg, char **argv) {
	(void)arg;
	(void)argv;
	syscall_putchar('[');
	syscall_putchar('R');
	syscall_putchar(']');
	(void)syscall_env_destroy(0);
	for (;;) {
		syscall_yield();
	}
}
