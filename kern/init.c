#include <arch/csr.h>
#include <arch/trap.h>
#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>

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

	env_init();
	sched_init();
	env_create_kernel_demo("idle", 1);
	env_create_kernel_demo("worker-a", 2);
	env_create_kernel_demo("worker-b", 1);

	trap_init();
	printk("  trap handler installed at 0x%016lx\n", csr_read_stvec());
	printk("  waiting for 5 timer interrupts and scheduler rotations...\n");
	while (timer_ticks < 5) {
		wfi();
	}
	printk("  observed %lu timer interrupts\n", timer_ticks);
	if (curenv != NULL) {
		printk("  current env after scheduling: 0x%lx (%s), runs=%lu\n", curenv->env_id,
		       curenv->env_name, curenv->env_runs);
	}
	panic("kernel bootstrap complete; next step is syscall and user-mode bring-up");
}
