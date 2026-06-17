#include <arch/context.h>
#include <env.h>
#include <printk.h>
#include <sched.h>
#include <syscall.h>

void sched_init(void) {
}

static struct Env *pick_next_env(int rotate_current) {
	struct Env *env = curenv;

	if (env != NULL && env->env_status == ENV_RUNNABLE && !rotate_current &&
	    env->env_ticks_left > 0) {
		return env;
	}
	if (env != NULL && env->env_status == ENV_RUNNABLE) {
		TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
		TAILQ_INSERT_TAIL(&env_sched_list, env, env_sched_link);
	}
	env = TAILQ_FIRST(&env_sched_list);
	if (env == NULL) {
		panic("schedule: no runnable envs");
	}
	if (env->env_ticks_left == 0) {
		env->env_ticks_left = env->env_pri == 0 ? 1 : env->env_pri;
	}
	return env;
}

void schedule(int yield) {
	struct Env *env = pick_next_env(yield);

	if (env->env_ticks_left > 0) {
		env->env_ticks_left--;
	}
	curenv = env;
	env->env_runs++;
#ifndef MOS_TEST_MODE
	printk("  schedule -> env=0x%lx runs=%lu ticks_left=%u name=%s\n",
	       env->env_id, env->env_runs, env->env_ticks_left,
	       env->env_name[0] ? env->env_name : "(unnamed)");
#endif
	env_pop_tf(&env->env_tf, env->env_satp);
}

void sched_tick(void) {
	schedule(0);
}

void sched_yield(void) {
	schedule(1);
}
