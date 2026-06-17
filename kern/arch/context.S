.equ TF_RA, 8
.equ TF_SP, 16
.equ TF_GP, 24
.equ TF_TP, 32
.equ TF_T1, 48
.equ TF_T2, 56
.equ TF_S0, 64
.equ TF_S1, 72
.equ TF_A0, 80
.equ TF_A1, 88
.equ TF_A2, 96
.equ TF_A3, 104
.equ TF_A4, 112
.equ TF_A5, 120
.equ TF_A6, 128
.equ TF_A7, 136
.equ TF_S2, 144
.equ TF_S3, 152
.equ TF_S4, 160
.equ TF_S5, 168
.equ TF_S6, 176
.equ TF_S7, 184
.equ TF_S8, 192
.equ TF_S9, 200
.equ TF_S10, 208
.equ TF_S11, 216
.equ TF_T3, 224
.equ TF_T4, 232
.equ TF_T5, 240
.equ TF_T6, 248
.equ TF_SSTATUS, 256
.equ TF_SEPC, 264

.section .text
.global env_pop_tf
env_pop_tf:
	csrw satp, a1
	sfence.vma x0, x0
	la t6, boot_stack_top
	csrw sscratch, t6

	ld t6, TF_SSTATUS(a0)
	csrw sstatus, t6
	ld t6, TF_SEPC(a0)
	csrw sepc, t6

	ld ra,  TF_RA(a0)
	ld sp,  TF_SP(a0)
	ld gp,  TF_GP(a0)
	ld tp,  TF_TP(a0)
	ld t1,  TF_T1(a0)
	ld t2,  TF_T2(a0)
	ld s0,  TF_S0(a0)
	ld s1,  TF_S1(a0)
	ld a1,  TF_A1(a0)
	ld a2,  TF_A2(a0)
	ld a3,  TF_A3(a0)
	ld a4,  TF_A4(a0)
	ld a5,  TF_A5(a0)
	ld a6,  TF_A6(a0)
	ld a7,  TF_A7(a0)
	ld s2,  TF_S2(a0)
	ld s3,  TF_S3(a0)
	ld s4,  TF_S4(a0)
	ld s5,  TF_S5(a0)
	ld s6,  TF_S6(a0)
	ld s7,  TF_S7(a0)
	ld s8,  TF_S8(a0)
	ld s9,  TF_S9(a0)
	ld s10, TF_S10(a0)
	ld s11, TF_S11(a0)
	ld t3,  TF_T3(a0)
	ld t4,  TF_T4(a0)
	ld t5,  TF_T5(a0)
	ld t6,  TF_T6(a0)
	ld a0,  TF_A0(a0)
	sret
