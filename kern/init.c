#include <arch/csr.h>
#include <arch/trap.h>
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

	trap_init();
	printk("  trap handler installed at 0x%016lx\n", csr_read_stvec());
	printk("  waiting for 3 timer interrupts...\n");
	while (timer_ticks < 3) {
		wfi();
	}
	printk("  observed %lu timer interrupts\n", timer_ticks);
	panic("kernel bootstrap complete; next step is env and syscall bring-up");
}
