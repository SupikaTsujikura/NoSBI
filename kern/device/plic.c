#include <arch/vm.h>
#include <plic.h>
#include <printk.h>

#define PLIC_PRIORITY_BASE 0x000000UL
#define PLIC_PENDING_BASE  0x001000UL
#define PLIC_ENABLE_BASE   0x002000UL
#define PLIC_CONTEXT_BASE  0x200000UL

#define PLIC_CONTEXT_STRIDE 0x1000UL
#define PLIC_ENABLE_STRIDE  0x80UL

/*
 * QEMU virt context layout for hart 0 is:
 * context 0: M-mode, context 1: S-mode.
 * OpenSBI keeps M-mode; the kernel uses S-mode context 1.
 */
#define PLIC_S_CONTEXT 1UL

static volatile u32 *plic_reg(uintptr_t offset) {
	return (volatile u32 *)(PLIC_BASE + offset);
}

static uintptr_t enable_offset(u32 irq) {
	return PLIC_ENABLE_BASE + PLIC_S_CONTEXT * PLIC_ENABLE_STRIDE +
	       (uintptr_t)(irq / 32U) * sizeof(u32);
}

static uintptr_t threshold_offset(void) {
	return PLIC_CONTEXT_BASE + PLIC_S_CONTEXT * PLIC_CONTEXT_STRIDE;
}

static uintptr_t claim_offset(void) {
	return threshold_offset() + sizeof(u32);
}

void plic_init(void) {
	*plic_reg(threshold_offset()) = 0;
	*plic_reg(PLIC_PRIORITY_BASE + PLIC_IRQ_UART0 * sizeof(u32)) = 1;
	*plic_reg(PLIC_PRIORITY_BASE + PLIC_IRQ_VIRTIO0 * sizeof(u32)) = 1;
	printk("  plic init context=%lu pending=0x%x\n", PLIC_S_CONTEXT,
	       *plic_reg(PLIC_PENDING_BASE));
}

void plic_enable_irq(u32 irq) {
	if (irq == 0 || irq >= 1024) {
		return;
	}
	*plic_reg(enable_offset(irq)) |= 1U << (irq % 32U);
}

u32 plic_claim(void) {
	return *plic_reg(claim_offset());
}

void plic_complete(u32 irq) {
	*plic_reg(claim_offset()) = irq;
}
