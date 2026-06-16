#include <arch/csr.h>
#include <arch/trap.h>
#include <print.h>
#include <printk.h>

void outputk(void *data, const char *buf, size_t len);

static void dump_trap_state(void) {
	printk("sstatus=%016lx sepc=%016lx scause=%016lx stval=%016lx stvec=%016lx\n",
	       csr_read_sstatus(), csr_read_sepc(), csr_read_scause(), csr_read_stval(),
	       csr_read_stvec());
}

void panic_here(const char *file, int line, const char *func, const char *fmt, ...) {
	printk("panic at %s:%d (%s): ", file, line, func);

	va_list ap;
	va_start(ap, fmt);
	vprintfmt(outputk, NULL, fmt, ap);
	va_end(ap);

	printk("\n");
	dump_trap_state();
	halt();
}

void print_tf(struct Trapframe *tf) {
	for (size_t i = 0; i < ARRAY_SIZE(tf->regs); i++) {
		printk("x%-2lu = %016lx\n", i, tf->regs[i]);
	}
	printk("sstatus = %016lx\n", tf->sstatus);
	printk("sepc    = %016lx\n", tf->sepc);
	printk("stval   = %016lx\n", tf->stval);
	printk("scause  = %016lx\n", tf->scause);
}
