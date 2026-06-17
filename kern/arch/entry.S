.equ TRAPFRAME_SIZE, 288

.section .text
.align 2
.global trap_entry
trap_entry:
	csrr t6, sstatus
	andi t6, t6, 0x100
	bnez t6, kernel_trap_entry
	csrrw sp, sscratch, sp
	addi sp, sp, -TRAPFRAME_SIZE
	sd zero, 0(sp)
	sd ra,   8(sp)
	sd t6, 248(sp)
	csrr t6, sscratch
	sd t6,  16(sp)
	sd gp,  24(sp)
	sd tp,  32(sp)
	sd t0,  40(sp)
	sd t1,  48(sp)
	sd t2,  56(sp)
	sd s0,  64(sp)
	sd s1,  72(sp)
	sd a0,  80(sp)
	sd a1,  88(sp)
	sd a2,  96(sp)
	sd a3, 104(sp)
	sd a4, 112(sp)
	sd a5, 120(sp)
	sd a6, 128(sp)
	sd a7, 136(sp)
	sd s2, 144(sp)
	sd s3, 152(sp)
	sd s4, 160(sp)
	sd s5, 168(sp)
	sd s6, 176(sp)
	sd s7, 184(sp)
	sd s8, 192(sp)
	sd s9, 200(sp)
	sd s10, 208(sp)
	sd s11, 216(sp)
	sd t3, 224(sp)
	sd t4, 232(sp)
	sd t5, 240(sp)

	csrr t6, sstatus
	sd t6, 256(sp)
	csrr t6, sepc
	sd t6, 264(sp)
	csrr t6, stval
	sd t6, 272(sp)
	csrr t6, scause
	sd t6, 280(sp)

	mv a0, sp
	call trap_entry_c

	ld t6, 256(sp)
	csrw sstatus, t6
	ld t6, 264(sp)
	csrw sepc, t6

	ld ra,   8(sp)
	ld gp,  24(sp)
	ld tp,  32(sp)
	ld t0,  40(sp)
	ld t1,  48(sp)
	ld t2,  56(sp)
	ld s0,  64(sp)
	ld s1,  72(sp)
	ld a0,  80(sp)
	ld a1,  88(sp)
	ld a2,  96(sp)
	ld a3, 104(sp)
	ld a4, 112(sp)
	ld a5, 120(sp)
	ld a6, 128(sp)
	ld a7, 136(sp)
	ld s2, 144(sp)
	ld s3, 152(sp)
	ld s4, 160(sp)
	ld s5, 168(sp)
	ld s6, 176(sp)
	ld s7, 184(sp)
	ld s8, 192(sp)
	ld s9, 200(sp)
	ld s10, 208(sp)
	ld s11, 216(sp)
	ld t3, 224(sp)
	ld t4, 232(sp)
	ld t5, 240(sp)
	la t6, boot_stack_top
	csrw sscratch, t6
	ld t6, 248(sp)
	ld sp,  16(sp)
	sret

kernel_trap_entry:
	csrr a0, scause
	csrr a1, sepc
	csrr a2, stval
	la sp, boot_stack_top
	call kernel_trap_panic
1:
	wfi
	j 1b
