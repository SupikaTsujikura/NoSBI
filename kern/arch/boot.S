#include <arch/riscv.h>

.section .text.boot
.global _start
_start:
	la sp, boot_stack_top
	la t0, bss_start
	la t1, bss_end
1:
	bgeu t0, t1, 2f
	sd zero, 0(t0)
	addi t0, t0, 8
	j 1b
2:
	call kmain
1:
	wfi
	j 1b

.section .bss.stack
.align 16
.global boot_stack
boot_stack:
	.skip KERNEL_STACK_SIZE
.global boot_stack_top
boot_stack_top:
