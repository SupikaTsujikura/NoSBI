#include <arch/csr.h>
#include <arch/sbi.h>
#include <arch/trap.h>
#include <error.h>
#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>
#include <syscall.h>

#define TIMER_INTERVAL 1000000UL

volatile uint64_t timer_ticks;

extern char trap_entry[];
extern struct Env envs[];

static void timer_arm_next(void) {
	sbi_set_timer(csr_read_time() + TIMER_INTERVAL);
}

#ifdef MOS_TEST_MODE
static void maybe_finish_test(void) {
	if (envs[0].env_runs >= 8 && envs[1].env_runs >= 8 && timer_ticks >= 2) {
		printk("TEST PASS: bootstrap, vm, trap, timer, user mode, syscall, and scheduling verified\n");
		sbi_shutdown();
	}
}
#endif

static void handle_interrupt(struct Trapframe *tf) {
	reg_t cause = tf->scause & SCAUSE_CODE_MASK;

	switch (cause) {
	case SCAUSE_SUPERVISOR_TIMER:
		timer_ticks++;
#ifndef MOS_TEST_MODE
		printk("  timer interrupt #%lu\n", timer_ticks);
#endif
		timer_arm_next();
#ifdef MOS_TEST_MODE
		maybe_finish_test();
#endif
		sched_tick();
		break;
	default:
		panic("unhandled interrupt scause=%016lx", tf->scause);
	}
}

static int copy_to_curenv_user(uint64_t va, const void *src, size_t len) {
	const char *in = src;

	for (size_t i = 0; i < len; i++) {
		paddr_t pa;

		if (curenv == NULL || translate(curenv->env_pgtable, va + i, &pa, NULL) < 0) {
			return -E_INVAL;
		}
		*(char *)(uintptr_t)pa = in[i];
	}
	return 0;
}

static int dispatch_user_page_fault(struct Trapframe *tf) {
	uint64_t frame_va;
	uint64_t sp;

	if (curenv == NULL || curenv->env_user_tlb_mod_entry == 0) {
		return -E_INVAL;
	}
	sp = curenv->env_tf.regs[2];
	if (sp >= UXSTACKBASE && sp <= UXSTACKTOP) {
		frame_va = sp - sizeof(reg_t) - sizeof(struct Trapframe);
	} else {
		frame_va = UXSTACKTOP - sizeof(struct Trapframe);
	}
	frame_va &= ~0xfUL;
	if (frame_va < UXSTACKBASE) {
		return -E_NO_MEM;
	}
	try(copy_to_curenv_user(frame_va, tf, sizeof(*tf)));
	curenv->env_tf.regs[2] = frame_va;
	curenv->env_tf.regs[10] = frame_va;
	curenv->env_tf.sepc = curenv->env_user_tlb_mod_entry;
	return 0;
}

static void handle_exception(struct Trapframe *tf) {
	switch (tf->scause) {
	case SCAUSE_ECALL_FROM_U:
		if (curenv == NULL) {
			panic("ecall without current env");
		}
		curenv->env_tf.sepc += 4;
		syscall_dispatch();
		break;
	case SCAUSE_STORE_PAGE_FAULT:
		if (curenv != NULL && vm_handle_cow_fault(curenv->env_pgtable, tf->stval) == 0) {
			schedule(0);
		}
		if (dispatch_user_page_fault(tf) == 0) {
			schedule(0);
		}
		print_tf(tf);
		panic("unhandled store page fault va=%016lx", tf->stval);
		break;
	case SCAUSE_LOAD_PAGE_FAULT:
	case SCAUSE_INST_PAGE_FAULT:
		if (dispatch_user_page_fault(tf) == 0) {
			schedule(0);
		}
		print_tf(tf);
		panic("unhandled page fault va=%016lx", tf->stval);
		break;
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

void kernel_trap_panic(reg_t scause, reg_t sepc, reg_t stval) {
	panic("kernel trap scause=%016lx sepc=%016lx stval=%016lx", scause, sepc, stval);
}

void trap_entry_c(struct Trapframe *tf) {
	if (curenv != NULL) {
		curenv->env_tf = *tf;
	}
	if (tf->scause & SCAUSE_INTERRUPT) {
		handle_interrupt(tf);
	} else {
		handle_exception(tf);
	}
}
