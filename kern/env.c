#include <arch/context.h>
#include <arch/csr.h>
#include <arch/riscv.h>
#include <arch/vm.h>
#include <elf.h>
#include <env.h>
#include <error.h>
#include <fs.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>
#include <string.h>

struct Env envs[NENV] __attribute__((aligned(PAGE_SIZE)));
struct Env *curenv;
struct Env_list env_free_list;
struct Env_sched_list env_sched_list;

void syscall_close_env_fds(struct Env *env);

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
	for (size_t i = 0; i < PX(PT_LEVELS - 1, USER_TOP); i++) {
		env->env_pgtable[i] = 0;
	}
	try(vm_map_kernel_devices(env->env_pgtable));
	panic_on(vm_install_user_selfmap(env->env_pgtable));
	env->env_satp = SATP_MODE_SV39 | (page2pa(root_page) >> PAGE_SHIFT);
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

int envid2env(uint64_t envid, struct Env **penv, int checkperm) {
	struct Env *env;

	if (penv == NULL) {
		return -E_INVAL;
	}
	if (envid == 0) {
		if (curenv == NULL) {
			return -E_BAD_ENV;
		}
		*penv = curenv;
		return 0;
	}

	env = &envs[ENVX(envid)];
	if (env->env_status == ENV_FREE || env->env_id != envid) {
		return -E_BAD_ENV;
	}
	if (checkperm && curenv != NULL &&
	    env != curenv && env->env_parent_id != curenv->env_id) {
		return -E_BAD_ENV;
	}
	*penv = env;
	return 0;
}

static int elf_is_valid(const struct Elf64_Ehdr *ehdr, size_t size) {
	if (ehdr == NULL || size < sizeof(*ehdr)) {
		return 0;
	}
	if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
	    ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
		return 0;
	}
	if (ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB) {
		return 0;
	}
	if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_RISCV) {
		return 0;
	}
	if (ehdr->e_phoff + (size_t)ehdr->e_phnum * ehdr->e_phentsize > size ||
	    ehdr->e_phentsize != sizeof(struct Elf64_Phdr)) {
		return 0;
	}
	return 1;
}

static uint64_t elf_perm_to_pte(uint32_t flags) {
	uint64_t perm = PTE_U | PTE_A;

	if (flags & PF_R) {
		perm |= PTE_R;
	}
	if (flags & PF_W) {
		perm |= PTE_R | PTE_W | PTE_D;
	}
	if (flags & PF_X) {
		perm |= PTE_X;
	}
	return perm;
}

static int env_map_segment_page(struct Env *env, vaddr_t va, const u8 *src,
                                size_t src_off, size_t src_len,
                                uint64_t perm) {
	struct Page *page;

	if (va >= USER_MAP_TOP || (va & (PAGE_SIZE - 1)) != 0) {
		return -E_INVAL;
	}
	try(page_alloc(&page));
	if (src != NULL && src_len != 0) {
		memcpy((u8 *)page2kva(page) + src_off, src, src_len);
	}
	return page_insert(env->env_pgtable, page, va, perm);
}

int env_load_elf(struct Env *env, const void *image, size_t size) {
	const struct Elf64_Ehdr *ehdr = image;
	const u8 *bytes = image;

	if (env == NULL || !elf_is_valid(ehdr, size)) {
		return -E_INVAL;
	}
	for (u16 i = 0; i < ehdr->e_phnum; i++) {
		const struct Elf64_Phdr *ph =
		    (const struct Elf64_Phdr *)(bytes + ehdr->e_phoff + (size_t)i * ehdr->e_phentsize);
		vaddr_t start;
		vaddr_t end;
		vaddr_t va;
		uint64_t perm;

		if (ph->p_type != PT_LOAD) {
			continue;
		}
		if (ph->p_memsz < ph->p_filesz || ph->p_offset + ph->p_filesz > size ||
		    ph->p_vaddr + ph->p_memsz < ph->p_vaddr ||
		    ph->p_vaddr + ph->p_memsz > USER_MAP_TOP) {
			return -E_INVAL;
		}
		start = ROUNDDOWN(ph->p_vaddr, PAGE_SIZE);
		end = ROUND(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
		perm = elf_perm_to_pte(ph->p_flags);
		for (va = start; va < end; va += PAGE_SIZE) {
			size_t page_file_start = va < ph->p_vaddr ? 0 : va - ph->p_vaddr;
			size_t page_file_end = page_file_start + PAGE_SIZE;
			size_t copy_start;
			size_t copy_end;
			size_t src_off = 0;
			size_t src_len = 0;

			if (page_file_start < ph->p_filesz) {
				copy_start = page_file_start;
				copy_end = page_file_end < ph->p_filesz ? page_file_end : ph->p_filesz;
				src_off = (ph->p_vaddr + copy_start) & (PAGE_SIZE - 1);
				src_len = copy_end - copy_start;
			}
			try(env_map_segment_page(env, va, bytes + ph->p_offset + page_file_start,
			                         src_off, src_len, perm));
		}
	}
	memset(&env->env_tf, 0, sizeof(env->env_tf));
	env->env_tf.sepc = ehdr->e_entry;
	env->env_tf.sstatus = SSTATUS_SPIE;
	env->env_tf.regs[2] = USER_STACK_TOP;
	{
		struct Page *stack_page;
		struct Page *uxstack_page;

		try(page_alloc(&uxstack_page));
		try(page_insert(env->env_pgtable, uxstack_page, UXSTACKBASE,
		                PTE_R | PTE_W | PTE_U | PTE_A | PTE_D));
		try(page_alloc(&stack_page));
		try(page_insert(env->env_pgtable, stack_page, USER_STACK_BASE,
		                PTE_R | PTE_W | PTE_U | PTE_A | PTE_D));
	}
	return 0;
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
	env->env_ticks_left = env->env_pri;
	env->env_runs = 0;
	env->env_pgtable = NULL;
	env->env_satp = 0;
	env->env_user_tlb_mod_entry = 0;
	env->env_ipc_value = 0;
	env->env_ipc_from = 0;
	env->env_ipc_recving = 0;
	env->env_ipc_dstva = 0;
	env->env_ipc_perm = 0;
	memset(env->env_fds, 0, sizeof(env->env_fds));
	env_set_name(env, name);
	panic_on(env_setup_vm(env));
	*new_env = env;
	return 0;
}

void env_free(struct Env *env) {
	struct Page *root_page;

	if (env == NULL || env->env_status == ENV_FREE) {
		return;
	}
	printk("  env free: id=0x%lx name=%s\n", env->env_id,
	       env->env_name[0] ? env->env_name : "(unnamed)");
	if (env->env_status == ENV_RUNNABLE) {
		TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
	}
	if (env->env_pgtable != NULL) {
		root_page = pa2page((env->env_satp & ((1UL << 44) - 1)) << PAGE_SHIFT);
		vm_free_user_pagetable(env->env_pgtable, USER_TOP);
		page_decref(root_page);
	}
	syscall_close_env_fds(env);
	memset(&env->env_tf, 0, sizeof(env->env_tf));
	env->env_id = 0;
	env->env_parent_id = 0;
	env->env_status = ENV_FREE;
	env->env_pgtable = NULL;
	env->env_satp = 0;
	env->env_ticks_left = 0;
	env->env_runs = 0;
	env->env_user_tlb_mod_entry = 0;
	env->env_ipc_value = 0;
	env->env_ipc_from = 0;
	env->env_ipc_recving = 0;
	env->env_ipc_dstva = 0;
	env->env_ipc_perm = 0;
	env->env_name[0] = '\0';
	memset(env->env_fds, 0, sizeof(env->env_fds));
	LIST_INSERT_HEAD(&env_free_list, env, env_link);
}

void env_destroy(struct Env *env) {
	int was_current = env == curenv;

	env_free(env);
	if (was_current) {
		curenv = NULL;
		sched_yield();
	}
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

struct Env *env_create_elf(const char *name, const void *image, size_t size, uint32_t priority) {
	struct Env *env;

	if (env_alloc(&env, 0, name, priority) < 0) {
		return NULL;
	}
	if (env_load_elf(env, image, size) < 0) {
		env_free(env);
		return NULL;
	}
	env_set_status(env, ENV_RUNNABLE);
	printk("  elf env created: id=0x%lx pri=%lu name=%s entry=0x%lx\n", env->env_id,
	       (u_long)env->env_pri, env->env_name[0] ? env->env_name : "(unnamed)", env->env_tf.sepc);
	return env;
}

struct Env *env_create_path(const char *name, const char *path, uint32_t priority) {
	const void *image;
	size_t size;

	if (fs_read_all(path, &image, &size) < 0) {
		return NULL;
	}
	return env_create_elf(name, image, size, priority);
}
