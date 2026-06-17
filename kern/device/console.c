#include <arch/sbi.h>
#include <machine.h>

void printcharc(char ch) {
	if (ch == '\n') {
		sbi_console_putchar('\r');
	}
	sbi_console_putchar(ch);
}

int scancharc(void) {
	return 0;
}

void halt(void) {
	sbi_shutdown();
}
