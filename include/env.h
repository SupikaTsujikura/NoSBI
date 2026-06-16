#ifndef _MOS_RISCV_ENV_H_
#define _MOS_RISCV_ENV_H_

#include <arch/trap.h>
#include <queue.h>
#include <types.h>

#define LOG2NENV 10
#define NENV (1 << LOG2NENV)
#define ENVX(envid) ((envid) & (NENV - 1))

#define ENV_FREE 0
#define ENV_RUNNABLE 1
#define ENV_NOT_RUNNABLE 2

struct Env {
	struct Trapframe env_tf;
	LIST_ENTRY(Env) env_link;
	uint64_t env_id;
	uint64_t env_parent_id;
	uint32_t env_status;
	TAILQ_ENTRY(Env) env_sched_link;
	uint32_t env_pri;
	uint64_t env_runs;
};

LIST_HEAD(Env_list, Env);
TAILQ_HEAD(Env_sched_list, Env);

#endif
