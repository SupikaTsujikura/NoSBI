#include <arch/csr.h>
#include <arch/sbi.h>
#include <arch/trap.h>
#include <printk.h>

#define TIMER_INTERVAL 1000000UL

volatile uint64_t timer_ticks;

extern char trap_entry[];

static void timer_arm_next(void) {
	sbi_set_timer(csr_read_time() + TIMER_INTERVAL);
}

static void handle_interrupt(struct Trapframe *tf) {
	reg_t cause = tf->scause & SCAUSE_CODE_MASK;

	switch (cause) {
	case SCAUSE_SUPERVISOR_TIMER:
		timer_ticks++;
		printk("  timer interrupt #%lu\n", timer_ticks);
		timer_arm_next();
		break;
	default:
		panic("unhandled interrupt scause=%016lx", tf->scause);
	}
}

static void handle_exception(struct Trapframe *tf) {
	switch (tf->scause) {
	case SCAUSE_ECALL_FROM_U:
		panic("user ecall handling is not implemented yet");
	default:
		print_tf(tf);
		panic("unhandled exception scause=%016lx", tf->scause);
	}
}

void trap_init(void) {
	csr_write_stvec((reg_t)(uintptr_t)trap_entry);
	csr_write_sie(csr_read_sie() | SIE_STIE);
	timer_ticks = 0;
	timer_arm_next();
	csr_set_sstatus(SSTATUS_SIE);
}

void trap_entry_c(struct Trapframe *tf) {
	if (tf->scause & SCAUSE_INTERRUPT) {
		handle_interrupt(tf);
	} else {
		handle_exception(tf);
	}
}
