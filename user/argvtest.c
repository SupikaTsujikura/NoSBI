#include "lib.h"

static u_long argv_strlen(const char *s) {
	u_long len = 0;

	while (s[len] != '\0') {
		len++;
	}
	return len;
}

static void argv_puts(const char *s) {
	(void)syscall_print_cons(s, argv_strlen(s));
}

void user_main(long argc, char **argv) {
	if (argc == 3 && argv != 0 && argv[0] != 0 && argv[1] != 0 && argv[2] != 0 &&
	    argv[3] == 0) {
		argv_puts("[argv:");
		argv_puts(argv[0]);
		argv_puts(",");
		argv_puts(argv[1]);
		argv_puts(",");
		argv_puts(argv[2]);
		argv_puts("]");
	} else {
		argv_puts("[argv-fail]");
	}
	(void)syscall_env_destroy(0);
	for (;;) {
		syscall_yield();
	}
}
