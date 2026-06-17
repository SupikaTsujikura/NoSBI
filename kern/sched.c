#include <env.h>
#include <printk.h>
#include <sched.h>

static int sched_slices;

void sched_init(void) {
	sched_slices = 0;
}

static void env_run(struct Env *env) {
	struct Env *prev = curenv;
	curenv = env;
	env->env_runs++;
	if (prev != env) {
		printk("  schedule -> env=0x%lx runs=%lu name=%s\n", env->env_id, env->env_runs,
		       env->env_name[0] ? env->env_name : "(unnamed)");
	}
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
			curenv = NULL;
			return;
		}
		sched_slices = env->env_pri;
	}

	sched_slices--;
	env_run(env);
}
