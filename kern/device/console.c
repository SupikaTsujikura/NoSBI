#include <arch/sbi.h>
#include <machine.h>

void printcharc(char ch) {
	if (ch == '\n') {
		sbi_console_putchar('\r');
	}
	sbi_console_putchar(ch);
}

int scancharc(void) {
	int ch = sbi_console_getchar();

	return ch < 0 ? 0 : ch;
}

void halt(void) {
	sbi_shutdown();
}
