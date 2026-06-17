#include <arch/csr.h>
#include <arch/trap.h>
#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>

extern char trap_entry[];

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

	csr_write_stvec((reg_t)(uintptr_t)trap_entry);
	printk("  early trap vector installed at 0x%016lx\n", csr_read_stvec());

	printk("  env_init begin\n");
	env_init();
	printk("  env_init done\n");
	sched_init();
	printk("  sched_init done\n");
	env_create_user_demo("user-a", 1, 'A');
	printk("  user-a loaded\n");
	env_create_user_demo("user-b", 1, 'B');
	printk("  user-b loaded\n");

	trap_init();
	printk("  trap handler installed at 0x%016lx\n", csr_read_stvec());
	printk("  entering first user env via scheduler...\n");
	schedule(1);
	panic("schedule unexpectedly returned to kmain");
}
