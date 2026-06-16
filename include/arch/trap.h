#ifndef _MOS_RISCV_ARCH_TRAP_H_
#define _MOS_RISCV_ARCH_TRAP_H_

#include <types.h>

struct Trapframe {
	reg_t regs[32];
	reg_t sstatus;
	reg_t sepc;
	reg_t stval;
	reg_t scause;
};

void print_tf(struct Trapframe *tf);

#endif
