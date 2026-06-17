#include <arch/context.h>
#include <env.h>
#include <printk.h>
#include <sched.h>
#include <syscall.h>

static int sched_slices;

void sched_init(void) {
	sched_slices = 0;
}

void schedule(int yield) {
	struct Env *env = curenv;

	if (yield || sched_slices == 0 || env == NULL || env->env_status != ENV_RUNNABLE) {
		if (env != NULL && env->env_status == ENV_RUNNABLE) {
			TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
			TAILQ_INSERT_TAIL(&env_sched_list, env, env_sched_link);
		}
		env = TAILQ_FIRST(&env_sched_list);
		if (env == NULL) {
			panic("schedule: no runnable envs");
		}
		sched_slices = env->env_pri;
	}

	sched_slices--;
	curenv = env;
	env->env_runs++;
	printk("  schedule -> env=0x%lx runs=%lu name=%s\n", env->env_id, env->env_runs,
	       env->env_name[0] ? env->env_name : "(unnamed)");
	env_pop_tf(&env->env_tf, env->env_satp);
}
