#ifndef _MOS_RISCV_PLIC_H_
#define _MOS_RISCV_PLIC_H_

#include <types.h>

#define PLIC_IRQ_VIRTIO0 1
#define PLIC_IRQ_UART0 10

void plic_init(void);
void plic_enable_irq(u32 irq);
u32 plic_claim(void);
void plic_complete(u32 irq);

#endif
