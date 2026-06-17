#ifndef _MOS_RISCV_ENV_H_
#define _MOS_RISCV_ENV_H_

#include <arch/riscv.h>
#include <arch/trap.h>
#include <pmap.h>
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
	pagetable_t env_pgtable;
	reg_t env_satp;
	char env_name[16];
};

LIST_HEAD(Env_list, Env);
TAILQ_HEAD(Env_sched_list, Env);

extern struct Env envs[NENV] __attribute__((aligned(PAGE_SIZE)));
extern struct Env *curenv;
extern struct Env_list env_free_list;
extern struct Env_sched_list env_sched_list;

void env_init(void);
int env_alloc(struct Env **new_env, uint64_t parent_id, const char *name, uint32_t priority);
void env_set_status(struct Env *env, uint32_t status);
struct Env *env_create_user_demo(const char *name, uint32_t priority, int ch);

#endif
