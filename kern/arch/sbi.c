#include <arch/sbi.h>

#define SBI_EXT_LEGACY_CONSOLE_PUTCHAR 0x1
#define SBI_EXT_SRST 0x53525354
#define SBI_SRST_RESET_TYPE_SHUTDOWN 0x0
#define SBI_SRST_RESET_REASON_NONE 0x0

struct sbiret sbi_ecall(long ext, long fid, long arg0, long arg1, long arg2, long arg3,
			 long arg4, long arg5) {
	register long a0 asm("a0") = arg0;
	register long a1 asm("a1") = arg1;
	register long a2 asm("a2") = arg2;
	register long a3 asm("a3") = arg3;
	register long a4 asm("a4") = arg4;
	register long a5 asm("a5") = arg5;
	register long a6 asm("a6") = fid;
	register long a7 asm("a7") = ext;

	asm volatile("ecall"
		     : "+r"(a0), "+r"(a1)
		     : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
		     : "memory");

	return (struct sbiret){
		.error = a0,
		.value = a1,
	};
}

void sbi_console_putchar(int ch) {
	register long a0 asm("a0") = ch;
	register long a7 asm("a7") = SBI_EXT_LEGACY_CONSOLE_PUTCHAR;

	asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

void sbi_shutdown(void) {
	sbi_ecall(SBI_EXT_SRST, 0, SBI_SRST_RESET_TYPE_SHUTDOWN, SBI_SRST_RESET_REASON_NONE, 0, 0, 0,
		 0);
	for (;;) {
		asm volatile("wfi");
	}
}
