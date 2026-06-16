#include <arch/csr.h>
#include <pmap.h>
#include <printk.h>

void kmain(void) {
	csr_clear_sstatus(SSTATUS_SIE);
	printk("MOS/RISC-V bootstrap\n");
	printk("  sstatus = 0x%016lx\n", csr_read_sstatus());
	printk("  stvec   = 0x%016lx\n", csr_read_stvec());

	vm_bootstrap();
	printk("  npage   = %lu\n", npage);
	printk("  maxpa   = 0x%016lx\n", maxpa);
	printk("  pages   = %p\n", pages);
	printk("  kpt     = %p\n", kernel_pagetable);

	vm_self_test();
	printk("  vm self-test passed\n");

	vm_enable();
	printk("  satp enabled, kernel still alive\n");
	panic("kernel bootstrap complete; next step is trap and timer bring-up");
}
