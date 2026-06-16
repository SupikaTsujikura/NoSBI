#ifndef _MOS_RISCV_ARCH_SBI_H_
#define _MOS_RISCV_ARCH_SBI_H_

#include <types.h>

struct sbiret {
	long error;
	long value;
};

struct sbiret sbi_ecall(long ext, long fid, long arg0, long arg1, long arg2, long arg3,
			 long arg4, long arg5);
void sbi_console_putchar(int ch);
void sbi_shutdown(void) __attribute__((noreturn));

#endif
