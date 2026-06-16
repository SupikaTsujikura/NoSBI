#include <arch/csr.h>
#include <printk.h>

void kmain(void) {
	csr_clear_sstatus(SSTATUS_SIE);
	printk("MOS/RISC-V bootstrap\n");
	printk("  sstatus = 0x%016lx\n", csr_read_sstatus());
	printk("  stvec   = 0x%016lx\n", csr_read_stvec());
	panic("kernel bootstrap complete; next step is Sv39/trap bring-up");
}
