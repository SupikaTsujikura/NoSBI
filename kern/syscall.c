#include <env.h>
#include <machine.h>
#include <printk.h>
#include <sched.h>
#include <syscall.h>

void syscall_dispatch(void) {
	reg_t sysno = curenv->env_tf.regs[17];
	long ret = 0;

	switch (sysno) {
	case SYS_putchar:
		printcharc((char)curenv->env_tf.regs[10]);
		ret = 0;
		break;
	case SYS_getenvid:
		ret = (long)curenv->env_id;
		break;
	case SYS_yield:
		schedule(1);
		return;
	default:
		panic("unsupported syscall %lu", (u_long)sysno);
	}

	curenv->env_tf.regs[10] = (reg_t)ret;
	schedule(0);
}
