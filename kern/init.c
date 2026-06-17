#include <arch/csr.h>
#include <arch/trap.h>
#include <block.h>
#include <env.h>
#include <fs.h>
#include <pmap.h>
#include <plic.h>
#include <printk.h>
#include <sched.h>

extern char trap_entry[];

void kmain(void) {
	csr_clear_sstatus(SSTATUS_SIE);
	csr_clear_sstatus(SSTATUS_SUM);
	printk("MOS/RISC-V bootstrap\n");
	printk("  sstatus = 0x%016lx\n", csr_read_sstatus());
	printk("  stvec   = 0x%016lx\n", csr_read_stvec());
	printk("  satp    = 0x%016lx\n", csr_read_satp());
	printk("  sie     = 0x%016lx\n", csr_read_sie());
	printk("  sip     = 0x%016lx\n", csr_read_sip());
	printk("  time    = 0x%016lx\n", csr_read_time());

	vm_bootstrap();
	printk("  npage   = %lu\n", npage);
	printk("  maxpa   = 0x%016lx\n", maxpa);
	printk("  pages   = %p\n", pages);
	printk("  kpt     = %p\n", kernel_pagetable);

	vm_self_test();
	printk("  vm self-test passed\n");

	vm_enable();
	printk("  satp enabled: 0x%016lx, kernel still alive\n", csr_read_satp());

	csr_write_stvec((reg_t)(uintptr_t)trap_entry);
	printk("  early trap vector installed at 0x%016lx\n", csr_read_stvec());

	printk("  env_init begin\n");
	env_init();
	printk("  env_init done\n");
	plic_init();
	block_init();
	fs_init();
	printk("  fs_init done\n");
	sched_init();
	printk("  sched_init done\n");
	struct Env *user_a = env_create_path("user-a", "/bin/demo", 1);
	if (user_a == NULL) {
		panic("failed to create user-a from /bin/demo");
	}
	user_a->env_tf.regs[10] = 'A';
	printk("  user-a loaded\n");
	struct Env *user_b = env_create_path("user-b", "/bin/demo", 1);
	if (user_b == NULL) {
		panic("failed to create user-b from /bin/demo");
	}
	user_b->env_tf.regs[10] = 'B';
	printk("  user-b loaded\n");

	trap_init();
	printk("  trap handler installed at 0x%016lx\n", csr_read_stvec());
	printk("  entering first user env via scheduler...\n");
	schedule(1);
	panic("schedule unexpectedly returned to kmain");
}
