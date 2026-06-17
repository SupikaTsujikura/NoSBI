	.section .text
	.align 4
	.global _start
_start:
	call user_main
	li a0, 0
	li a7, 4
	ecall
1:
	li a7, 3
	ecall
	j 1b
