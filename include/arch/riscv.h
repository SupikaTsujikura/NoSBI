#ifndef _MOS_RISCV_ARCH_RISCV_H_
#define _MOS_RISCV_ARCH_RISCV_H_

#include <types.h>

#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define KERNEL_BASE 0x80200000UL
#define KERNEL_STACK_SIZE (16 * 1024UL)

#endif
