#include <arch/context.h>
#include <arch/csr.h>
#include <arch/riscv.h>
#include <arch/vm.h>
#include <env.h>
#include <error.h>
#include <pmap.h>
#include <printk.h>
#include <string.h>

#define USER_TEXT 0x00400000UL
#define USER_STACK_TOP 0x40000000UL
#define USER_STACK_BASE (USER_STACK_TOP - PAGE_SIZE)

extern char user_demo_start[];
extern char user_demo_end[];

struct Env envs[NENV] __attribute__((aligned(PAGE_SIZE)));
struct Env *curenv;
struct Env_list env_free_list;
struct Env_sched_list env_sched_list;

static uint64_t next_env_gen;

static void env_set_name(struct Env *env, const char *name) {
	size_t i = 0;
	if (name == NULL) {
		env->env_name[0] = '\0';
		return;
	}
	for (; i + 1 < sizeof(env->env_name) && name[i] != '\0'; i++) {
		env->env_name[i] = name[i];
	}
	env->env_name[i] = '\0';
}

static uint64_t mkenvid(struct Env *env) {
	next_env_gen++;
	return (next_env_gen << (1 + LOG2NENV)) | (uint64_t)(env - envs);
}

static int env_setup_vm(struct Env *env) {
	struct Page *root_page;
	panic_on(page_alloc(&root_page));
	root_page->pp_ref++;
	env->env_pgtable = (pagetable_t)page2kva(root_page);
	memcpy(env->env_pgtable, kernel_pagetable, PAGE_SIZE);
	env->env_satp = SATP_MODE_SV39 | (page2pa(root_page) >> PAGE_SHIFT);
	return 0;
}

static int env_load_demo(struct Env *env, int ch) {
	struct Page *code_page;
	struct Page *stack_page;
	size_t text_size = (size_t)(user_demo_end - user_demo_start);

	if (text_size > PAGE_SIZE) {
		return -E_NO_MEM;
	}

	panic_on(page_alloc(&code_page));
	panic_on(page_alloc(&stack_page));
	panic_on(page_insert(env->env_pgtable, code_page, USER_TEXT,
			     PTE_R | PTE_X | PTE_U | PTE_A));
	panic_on(page_insert(env->env_pgtable, stack_page, USER_STACK_BASE,
			     PTE_R | PTE_W | PTE_U | PTE_A | PTE_D));
	memcpy(page2kva(code_page), user_demo_start, text_size);

	memset(&env->env_tf, 0, sizeof(env->env_tf));
	env->env_tf.sepc = USER_TEXT;
	env->env_tf.sstatus = SSTATUS_SPIE;
	env->env_tf.regs[2] = USER_STACK_TOP;
	env->env_tf.regs[10] = (reg_t)ch;
	return 0;
}

void env_init(void) {
	LIST_INIT(&env_free_list);
	TAILQ_INIT(&env_sched_list);
	curenv = NULL;

	for (int i = NENV - 1; i >= 0; i--) {
		memset(&envs[i], 0, sizeof(envs[i]));
		envs[i].env_status = ENV_FREE;
		LIST_INSERT_HEAD(&env_free_list, &envs[i], env_link);
	}
}

int env_alloc(struct Env **new_env, uint64_t parent_id, const char *name, uint32_t priority) {
	struct Env *env = LIST_FIRST(&env_free_list);
	if (env == NULL) {
		return -E_NO_FREE_ENV;
	}

	LIST_REMOVE(env, env_link);
	memset(&env->env_tf, 0, sizeof(env->env_tf));
	env->env_id = mkenvid(env);
	env->env_parent_id = parent_id;
	env->env_status = ENV_NOT_RUNNABLE;
	env->env_pri = priority == 0 ? 1 : priority;
	env->env_runs = 0;
	env->env_pgtable = NULL;
	env->env_satp = 0;
	env_set_name(env, name);
	panic_on(env_setup_vm(env));
	*new_env = env;
	return 0;
}

void env_set_status(struct Env *env, uint32_t status) {
	if (env->env_status == ENV_RUNNABLE && status != ENV_RUNNABLE) {
		TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
	}
	if (env->env_status != ENV_RUNNABLE && status == ENV_RUNNABLE) {
		TAILQ_INSERT_TAIL(&env_sched_list, env, env_sched_link);
	}
	env->env_status = status;
}

struct Env *env_create_user_demo(const char *name, uint32_t priority, int ch) {
	struct Env *env;
	panic_on(env_alloc(&env, 0, name, priority));
	panic_on(env_load_demo(env, ch));
	env_set_status(env, ENV_RUNNABLE);
	printk("  env created: id=0x%lx pri=%lu name=%s entry=0x%lx\n", env->env_id,
	       (u_long)env->env_pri, env->env_name[0] ? env->env_name : "(unnamed)", env->env_tf.sepc);
	return env;
}
