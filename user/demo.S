	.section .rodata.userdemo
	.align 4
	.global user_demo_start
user_demo_start:
	mv s0, a0
	li s1, 8
1:
	mv a0, s0
	li a7, 0
	ecall
	addi s1, s1, -1
	beqz s1, 2f
	li a7, 3
	ecall
	j 1b
2:
	j 2b
	.global user_demo_end
user_demo_end:
