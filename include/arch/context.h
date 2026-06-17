#ifndef _MOS_RISCV_ARCH_CONTEXT_H_
#define _MOS_RISCV_ARCH_CONTEXT_H_

#include <arch/trap.h>
#include <types.h>

void env_pop_tf(struct Trapframe *tf, reg_t satp) __attribute__((noreturn));

#endif
