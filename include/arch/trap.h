#ifndef _MOS_RISCV_ARCH_TRAP_H_
#define _MOS_RISCV_ARCH_TRAP_H_

#include <types.h>

#define TRAPFRAME_REGS 32
#define TRAPFRAME_SIZE ((TRAPFRAME_REGS + 4) * sizeof(reg_t))

#define SCAUSE_CODE_MASK ((1UL << 63) - 1)
#define SCAUSE_SUPERVISOR_TIMER 5
#define SCAUSE_ECALL_FROM_U 8

struct Trapframe {
	reg_t regs[TRAPFRAME_REGS];
	reg_t sstatus;
	reg_t sepc;
	reg_t stval;
	reg_t scause;
};

extern volatile uint64_t timer_ticks;

void trap_init(void);
void trap_entry_c(struct Trapframe *tf);
void print_tf(struct Trapframe *tf);

#endif
