#ifndef _MOS_RISCV_ENV_H_
#define _MOS_RISCV_ENV_H_

#include <arch/riscv.h>
#include <arch/trap.h>
#include <pmap.h>
#include <queue.h>
#include <types.h>
#include <fs.h>

#define LOG2NENV 10
#define NENV (1 << LOG2NENV)
#define ENVX(envid) ((envid) & (NENV - 1))

#define ENV_FREE 0
#define ENV_RUNNABLE 1
#define ENV_NOT_RUNNABLE 2

/*
 * User virtual address layout for the RISC-V port.
 *
 * 0x0000_0000 .. USER_TEXT       : left unmapped to catch null pointers.
 * USER_TEXT    .. USTACKBASE     : user ELF text/data/heap and mmap-like pages.
 * USTACKBASE   .. USTACKTOP      : one-page initial user stack.
 * UXSTACKBASE  .. UXSTACKTOP     : one-page user exception stack.
 * UVPD         .. UVPT           : read-only view of the level-1 user page directory.
 * UVPT         .. USER_TOP       : read-only view of all level-0 user page tables.
 * USER_TOP     .. Sv39 upper half: reserved for supervisor mappings.
 *
 * Sv39 root entry 0 covers this whole 1GiB area.  The UVPT window consumes one
 * level-1 span: 512 page-table pages * 4KiB = 2MiB.  User code can inspect
 * mappings with ((uint64_t *)UVPT)[va >> 12], similar to 23241036's vpt/vpd.
 */
#define USER_TEXT 0x00400000UL
#define USER_TOP 0x40000000UL
#define UVPT (USER_TOP - SV39_LARGE_PAGE_SIZE)
#define UVPD (UVPT - PAGE_SIZE)
#define UXSTACKTOP UVPD
#define UXSTACKBASE (UXSTACKTOP - PAGE_SIZE)
#define USTACKTOP (UXSTACKBASE - PAGE_SIZE)
#define USER_STACK_TOP USTACKTOP
#define USER_STACK_BASE (USER_STACK_TOP - PAGE_SIZE)
#define USER_MAP_TOP UVPD
#define UTEMP (USER_TEXT - 2 * PAGE_SIZE)
#define UCOW (USER_TEXT - PAGE_SIZE)

#define VPN(va) ((uint64_t)(va) >> PAGE_SHIFT)
#define VPD ((volatile uint64_t *)UVPD)
#define VPT ((volatile uint64_t *)UVPT)

struct Env {
	struct Trapframe env_tf;
	LIST_ENTRY(Env) env_link;
	uint64_t env_id;
	uint64_t env_parent_id;
	uint32_t env_status;
	TAILQ_ENTRY(Env) env_sched_link;
	uint32_t env_pri;
	uint32_t env_ticks_left;
	uint64_t env_runs;
	pagetable_t env_pgtable;
	reg_t env_satp;
	uint64_t env_user_tlb_mod_entry;
	uint64_t env_ipc_value;
	uint64_t env_ipc_from;
	uint64_t env_ipc_recving;
	uint64_t env_ipc_dstva;
	uint64_t env_ipc_perm;
	char env_name[16];
	struct {
		int used;
		int openid;
	} env_fds[FS_MAX_FD];
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
int env_load_elf(struct Env *env, const void *image, size_t size);
struct Env *env_create_elf(const char *name, const void *image, size_t size, uint32_t priority);
struct Env *env_create_path(const char *name, const char *path, uint32_t priority);
int envid2env(uint64_t envid, struct Env **penv, int checkperm);
void env_free(struct Env *env);
void env_destroy(struct Env *env);

#endif
