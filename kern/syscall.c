#include <arch/csr.h>
#include <block.h>
#include <env.h>
#include <error.h>
#include <fs.h>
#include <machine.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>
#include <string.h>
#include <syscall.h>

struct KernelPipe {
	int used;
	int readers;
	int writers;
	size_t rpos;
	size_t wpos;
	uint8_t buf[FS_PIPE_CAPACITY];
};

struct KernelOpenFile {
	int used;
	int ref;
	int type;
	int fileid;
	int pipeid;
	size_t offset;
};

static struct KernelPipe pipes[FS_MAX_PIPES];
static struct KernelOpenFile open_files[FS_MAX_OPEN_FILES];

static int user_va_ok(vaddr_t va) {
	return va >= UTEMP && va < USER_MAP_TOP && (va & (PAGE_SIZE - 1)) == 0;
}

static int user_perm_ok(uint64_t perm) {
	const uint64_t allowed = PTE_FLAGS_MASK & ~PTE_G;

	if (perm == 0 || (perm & ~allowed) != 0) {
		return 0;
	}
	return 1;
}

static int user_perm_writable(uint64_t perm) {
	return (perm & (PTE_D | PTE_W)) != 0 && !(perm & PTE_COW);
}

static uint64_t user_perm_to_pte(uint64_t perm) {
	uint64_t pte_perm = PTE_U | PTE_R | PTE_A;

	if (user_perm_writable(perm)) {
		pte_perm |= PTE_W | PTE_D;
	}
	if (perm & PTE_X) {
		pte_perm |= PTE_X;
	}
	if (perm & PTE_COW) {
		pte_perm |= PTE_COW;
	}
	if (perm & PTE_LIBRARY) {
		pte_perm |= PTE_LIBRARY;
	}
	return pte_perm;
}

static uint64_t pte_to_user_perm(pte_t pte) {
	uint64_t perm = PTE_FLAGS(pte);

	if (perm & PTE_W) {
		perm |= PTE_D;
	}
	return perm;
}

static int copy_user_byte(uint64_t va, char *out) {
	paddr_t pa;
	pte_t *pte;

	if (curenv == NULL || out == NULL || va >= USER_TOP ||
	    translate(curenv->env_pgtable, va, &pa, &pte) < 0 ||
	    pte == NULL || ((*pte & (PTE_U | PTE_R)) != (PTE_U | PTE_R))) {
		return -E_INVAL;
	}
	*out = *(char *)(uintptr_t)pa;
	return 0;
}

static int copy_from_user(uint64_t va, void *dst, size_t len) {
	char *out = dst;

	for (size_t i = 0; i < len; i++) {
		try(copy_user_byte(va + i, &out[i]));
	}
	return 0;
}

static int copy_to_user(uint64_t va, const void *src, size_t len) {
	const char *in = src;

	for (size_t i = 0; i < len; i++) {
		paddr_t pa;
		pte_t *pte;

		if (curenv == NULL || va + i >= USER_TOP ||
		    translate(curenv->env_pgtable, va + i, &pa, &pte) < 0 ||
		    pte == NULL || ((*pte & (PTE_U | PTE_W)) != (PTE_U | PTE_W))) {
			return -E_INVAL;
		}
		*(char *)(uintptr_t)pa = in[i];
	}
	return 0;
}

static int copy_user_string(uint64_t va, char *dst, size_t max_len) {
	size_t i;

	if (dst == NULL || max_len == 0) {
		return -E_INVAL;
	}
	for (i = 0; i + 1 < max_len; i++) {
		try(copy_user_byte(va + i, &dst[i]));
		if (dst[i] == '\0') {
			return 0;
		}
	}
	dst[max_len - 1] = '\0';
	return -E_INVAL;
}

static int fd_alloc(struct Env *env) {
	if (env == NULL) {
		return -E_BAD_ENV;
	}
	for (int fd = 3; fd < FS_MAX_FD; fd++) {
		if (!env->env_fds[fd].used) {
			return fd;
		}
	}
	return -E_NO_MEM;
}

static struct KernelOpenFile *fd_file(struct Env *env, uint64_t fd) {
	int openid;

	if (env == NULL || fd >= FS_MAX_FD || !env->env_fds[fd].used) {
		return NULL;
	}
	openid = env->env_fds[fd].openid;
	if (openid < 0 || openid >= FS_MAX_OPEN_FILES || !open_files[openid].used) {
		return NULL;
	}
	return &open_files[openid];
}

static int ofile_alloc(void) {
	for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
		if (!open_files[i].used) {
			memset(&open_files[i], 0, sizeof(open_files[i]));
			open_files[i].used = 1;
			open_files[i].ref = 1;
			open_files[i].fileid = -1;
			open_files[i].pipeid = -1;
			return i;
		}
	}
	return -E_NO_MEM;
}

static void pipe_decref(int pipeid, int type) {
	if (pipeid < 0 || pipeid >= FS_MAX_PIPES || !pipes[pipeid].used) {
		return;
	}
	if (type == FD_PIPE_READ && pipes[pipeid].readers > 0) {
		pipes[pipeid].readers--;
	} else if (type == FD_PIPE_WRITE && pipes[pipeid].writers > 0) {
		pipes[pipeid].writers--;
	}
	if (pipes[pipeid].readers == 0 && pipes[pipeid].writers == 0) {
		memset(&pipes[pipeid], 0, sizeof(pipes[pipeid]));
	}
}

static void ofile_decref(int openid) {
	struct KernelOpenFile *file;

	if (openid < 0 || openid >= FS_MAX_OPEN_FILES || !open_files[openid].used) {
		return;
	}
	file = &open_files[openid];
	if (file->ref > 0) {
		file->ref--;
	}
	if (file->ref == 0) {
		if (file->type == FD_PIPE_READ || file->type == FD_PIPE_WRITE) {
			pipe_decref(file->pipeid, file->type);
		}
		memset(file, 0, sizeof(*file));
	}
}

static void fd_close(struct Env *env, int fd) {
	if (env == NULL || fd < 0 || fd >= FS_MAX_FD || !env->env_fds[fd].used) {
		return;
	}
	ofile_decref(env->env_fds[fd].openid);
	memset(&env->env_fds[fd], 0, sizeof(env->env_fds[fd]));
}

static void fds_incref_all(struct Env *env) {
	if (env == NULL) {
		return;
	}
	for (int fd = 0; fd < FS_MAX_FD; fd++) {
		int openid = env->env_fds[fd].openid;

		if (env->env_fds[fd].used && openid >= 0 && openid < FS_MAX_OPEN_FILES &&
		    open_files[openid].used) {
			open_files[openid].ref++;
		}
	}
}

static int inherit_user_fd_pages(struct Env *parent, struct Env *child) {
	if (parent == NULL || child == NULL) {
		return -E_INVAL;
	}
	for (int fd = 0; fd < FS_MAX_FD; fd++) {
		vaddr_t va = FS_USER_FDTABLE + (vaddr_t)fd * PAGE_SIZE;
		pte_t *pte = NULL;
		struct Page *pp = page_lookup(parent->env_pgtable, va, &pte);
		uint64_t perm;

		if (pp == NULL || pte == NULL || !(*pte & PTE_LIBRARY)) {
			continue;
		}
		perm = PTE_FLAGS(*pte);
		try(page_insert(child->env_pgtable, pp, va, perm));
	}
	memcpy(child->env_fds, parent->env_fds, sizeof(child->env_fds));
	fds_incref_all(child);
	return 0;
}

void syscall_close_env_fds(struct Env *env) {
	if (env == NULL) {
		return;
	}
	for (int fd = 0; fd < FS_MAX_FD; fd++) {
		fd_close(env, fd);
	}
}

void syscall_init(void) {
	memset(open_files, 0, sizeof(open_files));
	memset(pipes, 0, sizeof(pipes));
}

static long sys_print_cons(uint64_t str, uint64_t num) {
	for (uint64_t i = 0; i < num; i++) {
		char ch;

		if (copy_user_byte(str + i, &ch) < 0) {
			return -E_INVAL;
		}
		printcharc(ch);
	}
	return 0;
}

static long sys_set_tlb_mod_entry(uint64_t envid, uint64_t func) {
	struct Env *env;

	if (func >= USER_TOP) {
		return -E_INVAL;
	}
	try(envid2env(envid, &env, 1));
	env->env_user_tlb_mod_entry = func;
	return 0;
}

static long sys_env_destroy(uint64_t envid) {
	struct Env *env;

	try(envid2env(envid, &env, 1));
	env_destroy(env);
	return 0;
}

static long sys_mem_alloc(uint64_t envid, uint64_t va, uint64_t perm) {
	struct Env *env;
	struct Page *pp;
	int r;

	if (!user_va_ok(va) || !user_perm_ok(perm)) {
		return -E_INVAL;
	}
	try(envid2env(envid, &env, 1));
	try(page_alloc(&pp));
	r = page_insert(env->env_pgtable, pp, va, user_perm_to_pte(perm));
	if (r < 0) {
		page_free(pp);
		return r;
	}
	return 0;
}

static long sys_mem_map(uint64_t srcid, uint64_t srcva, uint64_t dstid,
                        uint64_t dstva, uint64_t perm) {
	struct Env *srcenv;
	struct Env *dstenv;
	struct Page *pp;
	pte_t *srcpte;
	uint64_t srcperm;
	uint64_t dstperm;

	if (!user_va_ok(srcva) || !user_va_ok(dstva) || !user_perm_ok(perm)) {
		return -E_INVAL;
	}
	try(envid2env(srcid, &srcenv, 1));
	try(envid2env(dstid, &dstenv, 1));
	pp = page_lookup(srcenv->env_pgtable, srcva, &srcpte);
	if (pp == NULL) {
		return -E_INVAL;
	}
	srcperm = pte_to_user_perm(*srcpte);
	dstperm = user_perm_to_pte(perm);
	if ((dstperm & PTE_W) && !(srcperm & (PTE_D | PTE_COW | PTE_LIBRARY))) {
		return -E_INVAL;
	}
	return page_insert(dstenv->env_pgtable, pp, dstva, dstperm);
}

static long sys_mem_unmap(uint64_t envid, uint64_t va) {
	struct Env *env;

	if (!user_va_ok(va)) {
		return -E_INVAL;
	}
	try(envid2env(envid, &env, 1));
	if (page_lookup(env->env_pgtable, va, NULL) == NULL) {
		return -E_INVAL;
	}
	page_remove(env->env_pgtable, va);
	return 0;
}

static long sys_mem_protect(uint64_t envid, uint64_t va, uint64_t perm) {
	struct Env *env;
	pte_t *pte;
	uint64_t oldperm;
	uint64_t newperm;

	if (!user_va_ok(va) || !user_perm_ok(perm)) {
		return -E_INVAL;
	}
	try(envid2env(envid, &env, 1));
	if (page_lookup(env->env_pgtable, va, &pte) == NULL) {
		return -E_INVAL;
	}
	oldperm = pte_to_user_perm(*pte);
	newperm = user_perm_to_pte(perm);
	if ((newperm & PTE_W) && !(oldperm & (PTE_D | PTE_COW | PTE_LIBRARY))) {
		return -E_INVAL;
	}
	return page_set_perm(env->env_pgtable, va, newperm);
}

static long sys_page_info(uint64_t va, uint64_t info_va) {
	struct UserPageInfo info;
	int r;

	if (curenv == NULL || info_va == 0 || va >= USER_TOP) {
		return -E_INVAL;
	}
	r = page_query(curenv->env_pgtable, va, &info);
	if (r < 0) {
		return r;
	}
	return copy_to_user(info_va, &info, sizeof(info));
}

static long sys_page_next(uint64_t start_va, uint64_t info_va) {
	struct UserPageInfo info;
	int r;

	if (curenv == NULL || info_va == 0) {
		return -E_INVAL;
	}
	if (start_va >= USER_TOP) {
		return 0;
	}
	r = page_next_mapped(curenv->env_pgtable, start_va, USER_TOP, &info);
	if (r <= 0) {
		return r;
	}
	try(copy_to_user(info_va, &info, sizeof(info)));
	return 1;
}

static long sys_exofork(void) {
	struct Env *child;

	if (curenv == NULL) {
		return -E_BAD_ENV;
	}
	try(env_alloc(&child, curenv->env_id, "child", curenv->env_pri));
	child->env_tf = curenv->env_tf;
	child->env_tf.regs[10] = 0;
	child->env_status = ENV_NOT_RUNNABLE;
	return (long)child->env_id;
}

static long sys_env_set_status(uint64_t envid, uint64_t status) {
	struct Env *env;

	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
		return -E_INVAL;
	}
	try(envid2env(envid, &env, 1));
	env_set_status(env, (uint32_t)status);
	if (env == curenv && status != ENV_RUNNABLE) {
		schedule(1);
	}
	return 0;
}

static long sys_set_trapframe(uint64_t envid, uint64_t tf_va) {
	struct Env *env;
	struct Trapframe tf;

	if (tf_va == 0) {
		return -E_INVAL;
	}
	try(envid2env(envid, &env, 1));
	try(copy_from_user(tf_va, &tf, sizeof(tf)));
	tf.sstatus &= ~SSTATUS_SPP;
	tf.sstatus |= SSTATUS_SPIE;
	env->env_tf = tf;
	return 0;
}

static long sys_panic(uint64_t msg) {
	char buf[128];
	uint64_t i;

	for (i = 0; i + 1 < sizeof(buf); i++) {
		if (copy_user_byte(msg + i, &buf[i]) < 0) {
			buf[i] = '\0';
			break;
		}
		if (buf[i] == '\0') {
			break;
		}
	}
	buf[sizeof(buf) - 1] = '\0';
	panic("user panic from env 0x%lx: %s", curenv ? curenv->env_id : 0, buf);
}

static long sys_ipc_recv(uint64_t dstva) {
	if (curenv == NULL) {
		return -E_BAD_ENV;
	}
	if (dstva != 0 && !user_va_ok(dstva)) {
		return -E_INVAL;
	}
	curenv->env_ipc_recving = 1;
	curenv->env_ipc_dstva = dstva;
	curenv->env_ipc_from = 0;
	curenv->env_ipc_perm = 0;
	curenv->env_tf.regs[10] = 0;
	env_set_status(curenv, ENV_NOT_RUNNABLE);
	schedule(1);
	return 0;
}

static long sys_ipc_try_send(uint64_t envid, uint64_t value,
                             uint64_t srcva, uint64_t perm) {
	struct Env *dstenv;

	if (srcva != 0 && !user_va_ok(srcva)) {
		return -E_INVAL;
	}
	if (srcva != 0 && !user_perm_ok(perm)) {
		return -E_INVAL;
	}
	try(envid2env(envid, &dstenv, 0));
	if (!dstenv->env_ipc_recving) {
		return -E_IPC_NOT_RECV;
	}
	if (srcva != 0 && dstenv->env_ipc_dstva != 0) {
		pte_t *srcpte;
		struct Page *pp = page_lookup(curenv->env_pgtable, srcva, &srcpte);
		uint64_t srcperm;

		if (pp == NULL || srcpte == NULL) {
			return -E_INVAL;
		}
		srcperm = pte_to_user_perm(*srcpte);
		if (user_perm_writable(perm) &&
		    !(srcperm & (PTE_D | PTE_COW | PTE_LIBRARY))) {
			return -E_INVAL;
		}
		try(page_insert(dstenv->env_pgtable, pp, dstenv->env_ipc_dstva,
		                user_perm_to_pte(perm)));
		dstenv->env_ipc_perm = perm;
	} else {
		dstenv->env_ipc_perm = 0;
	}
	dstenv->env_ipc_value = value;
	dstenv->env_ipc_from = curenv ? curenv->env_id : 0;
	dstenv->env_ipc_recving = 0;
	dstenv->env_tf.regs[10] = value;
	env_set_status(dstenv, ENV_RUNNABLE);
	return 0;
}

static long sys_ipc_info(uint64_t from_va, uint64_t perm_va) {
	if (curenv == NULL) {
		return -E_BAD_ENV;
	}
	if (from_va != 0) {
		try(copy_to_user(from_va, &curenv->env_ipc_from, sizeof(curenv->env_ipc_from)));
	}
	if (perm_va != 0) {
		try(copy_to_user(perm_va, &curenv->env_ipc_perm, sizeof(curenv->env_ipc_perm)));
	}
	return 0;
}

static long sys_cgetc(void) {
	int ch;

	do {
		ch = scancharc();
	} while (ch == 0);
	return ch;
}

static int sysdev_len_ok(uint64_t len) {
	return len == 1 || len == 2 || len == 4 || len == 8;
}

static int sysdev_range_ok(uint64_t pa, uint64_t len) {
	uint64_t end = pa + len;

	if (!sysdev_len_ok(len) || end < pa || (pa & (len - 1)) != 0) {
		return 0;
	}
	if (pa >= CLINT_BASE && end <= CLINT_BASE + 0x10000UL) {
		return 1;
	}
	if (pa >= PLIC_BASE && end <= PLIC_BASE + 2 * SV39_LARGE_PAGE_SIZE) {
		return 1;
	}
	if (pa >= UART0_BASE && end <= UART0_BASE + 0x100UL) {
		return 1;
	}
	if (pa >= VIRTIO0_BASE && end <= VIRTIO0_BASE + 8 * 0x1000UL) {
		return 1;
	}
	return 0;
}

static uint64_t kernel_satp(void) {
	return SATP_MODE_SV39 | (kva2pa(kernel_pagetable) >> PAGE_SHIFT);
}

static void mmio_copy_to_device(uint64_t pa, const uint8_t *src, uint64_t len) {
	volatile uint8_t *dev = (volatile uint8_t *)(uintptr_t)pa;
	uint64_t old_satp = csr_read_satp();

	csr_write_satp(kernel_satp());
	asm volatile("sfence.vma x0, x0" ::: "memory");
	for (uint64_t i = 0; i < len; i++) {
		dev[i] = src[i];
	}
	csr_write_satp(old_satp);
	asm volatile("sfence.vma x0, x0" ::: "memory");
}

static void mmio_copy_from_device(uint8_t *dst, uint64_t pa, uint64_t len) {
	volatile uint8_t *dev = (volatile uint8_t *)(uintptr_t)pa;
	uint64_t old_satp = csr_read_satp();

	csr_write_satp(kernel_satp());
	asm volatile("sfence.vma x0, x0" ::: "memory");
	for (uint64_t i = 0; i < len; i++) {
		dst[i] = dev[i];
	}
	csr_write_satp(old_satp);
	asm volatile("sfence.vma x0, x0" ::: "memory");
}

static long sys_write_dev(uint64_t src_va, uint64_t pa, uint64_t len) {
	uint8_t tmp[8];

	if (!sysdev_range_ok(pa, len)) {
		return -E_INVAL;
	}
	try(copy_from_user(src_va, tmp, len));
	mmio_copy_to_device(pa, tmp, len);
	return 0;
}

static long sys_read_dev(uint64_t dst_va, uint64_t pa, uint64_t len) {
	uint8_t tmp[8];

	if (!sysdev_range_ok(pa, len)) {
		return -E_INVAL;
	}
	mmio_copy_from_device(tmp, pa, len);
	return copy_to_user(dst_va, tmp, len);
}

static long sys_fs_open(uint64_t path_va, uint64_t flags) {
	char path[FS_PATH_MAX];
	int fileid;

	try(copy_user_string(path_va, path, sizeof(path)));
	fileid = fs_open_path(path, (int)flags);
	if (fileid < 0) {
		return fileid;
	}
	{
		int fd = fd_alloc(curenv);
		int openid;
		struct KernelOpenFile *ofile;

		if (fd < 0) {
			return fd;
		}
		openid = ofile_alloc();
		if (openid < 0) {
			return openid;
		}
		ofile = &open_files[openid];
		ofile->type = FD_FILE;
		ofile->fileid = fileid;
		curenv->env_fds[fd].used = 1;
		curenv->env_fds[fd].openid = openid;
		if (flags & FS_OPEN_APPEND) {
			struct FsStat stat;

			ofile->offset = 0;
			if (fs_stat_path(path, &stat) == 0) {
				ofile->offset = stat.size;
			}
		} else {
			ofile->offset = 0;
		}
		return fd;
	}
}

static long sys_fs_write(uint64_t fd, uint64_t buf_va, uint64_t len) {
	char tmp[128];
	size_t done = 0;
	struct KernelOpenFile *ofile = fd_file(curenv, fd);

	if (ofile == NULL) {
		return -E_INVAL;
	}
	if (ofile->type == FD_PIPE_WRITE) {
		struct KernelPipe *pipe;

		if (ofile->pipeid < 0 || ofile->pipeid >= FS_MAX_PIPES ||
		    !pipes[ofile->pipeid].used) {
			return -E_INVAL;
		}
		pipe = &pipes[ofile->pipeid];
		if (pipe->readers == 0) {
			return 0;
		}
		while (done < len && pipe->wpos - pipe->rpos < FS_PIPE_CAPACITY) {
			char ch;

			try(copy_user_byte(buf_va + done, &ch));
			pipe->buf[pipe->wpos % FS_PIPE_CAPACITY] = (uint8_t)ch;
			pipe->wpos++;
			done++;
		}
		return done == 0 ? -E_IPC_NOT_RECV : (long)done;
	}
	if (ofile->type != FD_FILE) {
		return -E_INVAL;
	}
	while (done < len) {
		size_t chunk = len - done;
		int n;

		if (chunk > sizeof(tmp)) {
			chunk = sizeof(tmp);
		}
		try(copy_from_user(buf_va + done, tmp, chunk));
		n = fs_write_file(ofile->fileid, ofile->offset + done, tmp, chunk);
		if (n < 0) {
			return n;
		}
		done += (size_t)n;
		if ((size_t)n < chunk) {
			break;
		}
	}
	ofile->offset += done;
	return (long)done;
}

static long sys_fs_read(uint64_t fd, uint64_t buf_va, uint64_t len) {
	char tmp[128];
	size_t done = 0;
	struct KernelOpenFile *ofile = fd_file(curenv, fd);

	if (ofile == NULL) {
		return -E_INVAL;
	}
	if (ofile->type == FD_PIPE_READ) {
		struct KernelPipe *pipe;

		if (ofile->pipeid < 0 || ofile->pipeid >= FS_MAX_PIPES ||
		    !pipes[ofile->pipeid].used) {
			return -E_INVAL;
		}
		pipe = &pipes[ofile->pipeid];
		while (done < len && pipe->rpos != pipe->wpos) {
			uint8_t ch = pipe->buf[pipe->rpos % FS_PIPE_CAPACITY];

			try(copy_to_user(buf_va + done, &ch, 1));
			pipe->rpos++;
			done++;
		}
		if (done == 0 && pipe->writers > 0) {
			return -E_IPC_NOT_RECV;
		}
		return (long)done;
	}
	if (ofile->type != FD_FILE) {
		return -E_INVAL;
	}
	while (done < len) {
		size_t chunk = len - done;
		int n;

		if (chunk > sizeof(tmp)) {
			chunk = sizeof(tmp);
		}
		n = fs_read_file(ofile->fileid, ofile->offset + done, tmp, chunk);
		if (n < 0) {
			return n;
		}
		if (n == 0) {
			break;
		}
		try(copy_to_user(buf_va + done, tmp, (size_t)n));
		done += (size_t)n;
		if ((size_t)n < chunk) {
			break;
		}
	}
	ofile->offset += done;
	return (long)done;
}

static long sys_fs_close(uint64_t fd) {
	if (fd >= FS_MAX_FD || !curenv->env_fds[fd].used) {
		return -E_INVAL;
	}
	fd_close(curenv, (int)fd);
	return 0;
}

static long sys_fs_stat(uint64_t path_va, uint64_t stat_va) {
	char path[FS_PATH_MAX];
	struct FsStat stat;

	try(copy_user_string(path_va, path, sizeof(path)));
	try(fs_stat_path(path, &stat));
	return copy_to_user(stat_va, &stat, sizeof(stat));
}

static long sys_fs_fstat(uint64_t fd, uint64_t stat_va) {
	struct FsStat stat;
	struct KernelOpenFile *ofile = fd_file(curenv, fd);

	if (stat_va == 0 || ofile == NULL) {
		return -E_INVAL;
	}
	if (ofile->type == FD_PIPE_READ || ofile->type == FD_PIPE_WRITE) {
		memset(&stat, 0, sizeof(stat));
		stat.type = FS_TYPE_FILE;
		stat.mode = 0600;
		stat.nlink = 1;
		return copy_to_user(stat_va, &stat, sizeof(stat));
	}
	if (ofile->type != FD_FILE) {
		return -E_INVAL;
	}
	try(fs_stat_file(ofile->fileid, &stat));
	return copy_to_user(stat_va, &stat, sizeof(stat));
}

static long sys_fs_seek(uint64_t fd, uint64_t off) {
	struct KernelOpenFile *ofile = fd_file(curenv, fd);

	if (ofile == NULL) {
		return -E_INVAL;
	}
	if (ofile->type != FD_FILE) {
		return -E_INVAL;
	}
	ofile->offset = (size_t)off;
	return 0;
}

static long sys_fs_dup(uint64_t oldfd, uint64_t newfd) {
	struct KernelOpenFile *ofile = fd_file(curenv, oldfd);

	if (newfd >= FS_MAX_FD || ofile == NULL) {
		return -E_INVAL;
	}
	if (oldfd == newfd) {
		return (long)newfd;
	}
	if (curenv->env_fds[newfd].used) {
		fd_close(curenv, (int)newfd);
	}
	curenv->env_fds[newfd] = curenv->env_fds[oldfd];
	ofile->ref++;
	return (long)newfd;
}

static long sys_fs_truncate(uint64_t fd, uint64_t size) {
	struct KernelOpenFile *ofile = fd_file(curenv, fd);

	if (ofile == NULL) {
		return -E_INVAL;
	}
	if (ofile->type != FD_FILE) {
		return -E_INVAL;
	}
	return fs_truncate_file(ofile->fileid, (size_t)size);
}

static int copy_user_u64(uint64_t va, uint64_t *out) {
	return copy_from_user(va, out, sizeof(*out));
}

static int copy_string_from_user_to_buf(uint64_t va, char *dst, size_t max_len,
                                        size_t *len_store) {
	size_t i;

	for (i = 0; i + 1 < max_len; i++) {
		try(copy_user_byte(va + i, &dst[i]));
		if (dst[i] == '\0') {
			if (len_store != NULL) {
				*len_store = i + 1;
			}
			return 0;
		}
	}
	return -E_INVAL;
}

static int setup_child_argv_stack(struct Env *child, uint64_t argv_va) {
	enum { MAX_ARGC = 16 };
	uint64_t argv_ptrs[MAX_ARGC];
	char argbuf[MAX_ARGC][128];
	size_t arglen[MAX_ARGC];
	uint8_t stack[PAGE_SIZE];
	uint64_t child_argv[MAX_ARGC + 1];
	int argc = 0;
	uint64_t sp = USER_STACK_TOP;
	uint64_t argv_child_va;
	paddr_t stack_pa;

	if (child == NULL || argv_va < UTEMP || argv_va >= USER_TOP) {
		return -E_INVAL;
	}
	memset(stack, 0, sizeof(stack));
	for (argc = 0; argc < MAX_ARGC; argc++) {
		try(copy_user_u64(argv_va + (uint64_t)argc * sizeof(uint64_t), &argv_ptrs[argc]));
		if (argv_ptrs[argc] == 0) {
			break;
		}
		if (argv_ptrs[argc] >= USER_TOP) {
			return -E_INVAL;
		}
		try(copy_string_from_user_to_buf(argv_ptrs[argc], argbuf[argc],
		                                 sizeof(argbuf[argc]), &arglen[argc]));
	}
	if (argc == MAX_ARGC) {
		return -E_INVAL;
	}
	for (int i = argc - 1; i >= 0; i--) {
		sp -= arglen[i];
		if (sp < USER_STACK_BASE) {
			return -E_NO_MEM;
		}
		memcpy(stack + (sp - USER_STACK_BASE), argbuf[i], arglen[i]);
		child_argv[i] = sp;
	}
	sp &= ~0xfUL;
	child_argv[argc] = 0;
	sp -= (uint64_t)(argc + 1) * sizeof(uint64_t);
	if (sp < USER_STACK_BASE) {
		return -E_NO_MEM;
	}
	argv_child_va = sp;
	memcpy(stack + (sp - USER_STACK_BASE), child_argv,
	       (size_t)(argc + 1) * sizeof(uint64_t));
	sp -= sizeof(uint64_t);
	if (sp < USER_STACK_BASE) {
		return -E_NO_MEM;
	}
	memset(stack + (sp - USER_STACK_BASE), 0, sizeof(uint64_t));
	try(translate(child->env_pgtable, USER_STACK_BASE, &stack_pa, NULL));
	memcpy((void *)(uintptr_t)stack_pa, stack, sizeof(stack));
	child->env_tf.regs[2] = sp;
	child->env_tf.regs[10] = (uint64_t)argc;
	child->env_tf.regs[11] = argv_child_va;
	return 0;
}

static long sys_pipe(uint64_t pfd_va) {
	int pipeid = -1;
	int rfd;
	int wfd;
	int ropenid;
	int wopenid;
	int out[2];

	if (pfd_va == 0) {
		return -E_INVAL;
	}
	for (int i = 0; i < FS_MAX_PIPES; i++) {
		if (!pipes[i].used) {
			pipeid = i;
			break;
		}
	}
	if (pipeid < 0) {
		return -E_NO_MEM;
	}
	rfd = fd_alloc(curenv);
	if (rfd < 0) {
		return rfd;
	}
	ropenid = ofile_alloc();
	if (ropenid < 0) {
		return ropenid;
	}
	curenv->env_fds[rfd].used = 1;
	curenv->env_fds[rfd].openid = ropenid;
	wfd = fd_alloc(curenv);
	if (wfd < 0) {
		fd_close(curenv, rfd);
		return wfd;
	}
	wopenid = ofile_alloc();
	if (wopenid < 0) {
		fd_close(curenv, rfd);
		return wopenid;
	}
	memset(&pipes[pipeid], 0, sizeof(pipes[pipeid]));
	pipes[pipeid].used = 1;
	pipes[pipeid].readers = 1;
	pipes[pipeid].writers = 1;

	curenv->env_fds[wfd].used = 1;
	curenv->env_fds[wfd].openid = wopenid;

	open_files[ropenid].type = FD_PIPE_READ;
	open_files[ropenid].pipeid = pipeid;
	open_files[wopenid].type = FD_PIPE_WRITE;
	open_files[wopenid].pipeid = pipeid;

	out[0] = rfd;
	out[1] = wfd;
	{
		int r = copy_to_user(pfd_va, out, sizeof(out));

		if (r < 0) {
			fd_close(curenv, rfd);
			fd_close(curenv, wfd);
			return r;
		}
	}
	return 0;
}

static long sys_spawn(uint64_t path_va, uint64_t arg) {
	char path[FS_PATH_MAX];
	const void *image;
	size_t size;
	struct Env *child;
	int r;

	try(copy_user_string(path_va, path, sizeof(path)));
	try(fs_read_all(path, &image, &size));
	try(env_alloc(&child, curenv ? curenv->env_id : 0, path, curenv ? curenv->env_pri : 1));
	r = env_load_elf(child, image, size);
	if (r < 0) {
		env_free(child);
		return r;
	}
	if (curenv != NULL) {
		r = inherit_user_fd_pages(curenv, child);
		if (r < 0) {
			env_free(child);
			return r;
		}
	}
	if (arg >= UTEMP && arg < USER_TOP) {
		r = setup_child_argv_stack(child, arg);
		if (r < 0) {
			env_free(child);
			return r;
		}
	} else {
		child->env_tf.regs[10] = arg;
	}
	env_set_status(child, ENV_RUNNABLE);
	return (long)child->env_id;
}

static long sys_env_status(uint64_t envid) {
	struct Env *env;

	if (envid2env(envid, &env, 1) < 0) {
		return ENV_FREE;
	}
	return env->env_status;
}

static long sys_env_find(uint64_t name_va) {
	char name[sizeof(envs[0].env_name)];

	try(copy_user_string(name_va, name, sizeof(name)));
	for (int i = 0; i < NENV; i++) {
		if (envs[i].env_status != ENV_FREE &&
		    strcmp(envs[i].env_name, name) == 0) {
			return (long)envs[i].env_id;
		}
	}
	return -E_BAD_ENV;
}

static int current_is_fsserv(void) {
	return curenv != NULL && strcmp(curenv->env_name, "fsserv") == 0;
}

static long sys_block_read(uint64_t sector, uint64_t buf_va) {
	uint8_t sector_buf[BLOCK_SECTOR_SIZE];
	int r;

	if (!current_is_fsserv()) {
		return -E_INVAL;
	}
	r = block_read_sector(sector, sector_buf);
	if (r < 0) {
		return r;
	}
	return copy_to_user(buf_va, sector_buf, sizeof(sector_buf));
}

static long sys_block_write(uint64_t sector, uint64_t buf_va) {
	uint8_t sector_buf[BLOCK_SECTOR_SIZE];

	if (!current_is_fsserv()) {
		return -E_INVAL;
	}
	try(copy_from_user(buf_va, sector_buf, sizeof(sector_buf)));
	return block_write_sector(sector, sector_buf);
}

static long sys_fork(void) {
	struct Env *child;

	if (curenv == NULL) {
		return -E_BAD_ENV;
	}
	try(env_alloc(&child, curenv->env_id, "fork", curenv->env_pri));
	child->env_tf = curenv->env_tf;
	child->env_tf.regs[10] = 0;
	child->env_user_tlb_mod_entry = curenv->env_user_tlb_mod_entry;
	memcpy(child->env_fds, curenv->env_fds, sizeof(child->env_fds));
	fds_incref_all(child);
	if (vm_copy_user_cow(curenv->env_pgtable, child->env_pgtable, USER_MAP_TOP) < 0) {
		syscall_close_env_fds(child);
		env_free(child);
		return -E_NO_MEM;
	}
	env_set_status(child, ENV_RUNNABLE);
	return (long)child->env_id;
}

static long sys_fs_unlink(uint64_t path_va) {
	char path[FS_PATH_MAX];

	try(copy_user_string(path_va, path, sizeof(path)));
	return fs_unlink_path(path);
}

static long sys_fs_rename(uint64_t old_va, uint64_t new_va) {
	char old_path[FS_PATH_MAX];
	char new_path[FS_PATH_MAX];

	try(copy_user_string(old_va, old_path, sizeof(old_path)));
	try(copy_user_string(new_va, new_path, sizeof(new_path)));
	return fs_rename_path(old_path, new_path);
}

static long sys_fs_mkdir(uint64_t path_va) {
	char path[FS_PATH_MAX];

	try(copy_user_string(path_va, path, sizeof(path)));
	return fs_mkdir_path(path);
}

static long sys_fs_chmod(uint64_t path_va, uint64_t mode) {
	char path[FS_PATH_MAX];

	try(copy_user_string(path_va, path, sizeof(path)));
	return fs_chmod_path(path, (u32)mode);
}

static long sys_fs_sync(void) {
	return fs_sync();
}

static long sys_fs_list(uint64_t index, uint64_t path_va, uint64_t path_len,
                        uint64_t stat_va) {
	char path[FS_PATH_MAX];
	struct FsStat stat;
	int r;

	if (path_len == 0) {
		return -E_INVAL;
	}
	r = fs_list((int)index, path, sizeof(path), stat_va == 0 ? NULL : &stat);
	if (r <= 0) {
		return r;
	}
	if (strlen(path) + 1 > path_len) {
		return -E_INVAL;
	}
	try(copy_to_user(path_va, path, strlen(path) + 1));
	if (stat_va != 0) {
		try(copy_to_user(stat_va, &stat, sizeof(stat)));
	}
	return r;
}

void syscall_dispatch(void) {
	reg_t sysno = curenv->env_tf.regs[17];
	long ret = 0;

	switch (sysno) {
	case SYS_putchar:
		if (curenv->env_tf.regs[10] > 0x7f) {
			ret = -E_INVAL;
		} else {
			printcharc((char)curenv->env_tf.regs[10]);
			ret = 0;
		}
		break;
	case SYS_print_cons:
		ret = sys_print_cons(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_getenvid:
		ret = (long)curenv->env_id;
		break;
	case SYS_yield:
		sched_yield();
		return;
	case SYS_env_destroy:
		ret = sys_env_destroy(curenv->env_tf.regs[10]);
		break;
	case SYS_set_tlb_mod_entry:
		ret = sys_set_tlb_mod_entry(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_mem_alloc:
		ret = sys_mem_alloc(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                    curenv->env_tf.regs[12]);
		break;
	case SYS_mem_map:
		ret = sys_mem_map(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                  curenv->env_tf.regs[12], curenv->env_tf.regs[13],
		                  curenv->env_tf.regs[14]);
		break;
	case SYS_mem_unmap:
		ret = sys_mem_unmap(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_mem_protect:
		ret = sys_mem_protect(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                      curenv->env_tf.regs[12]);
		break;
	case SYS_exofork:
		ret = sys_exofork();
		break;
	case SYS_env_set_status:
		ret = sys_env_set_status(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_set_trapframe:
		ret = sys_set_trapframe(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		if (ret == 0) {
			schedule(0);
			return;
		}
		break;
	case SYS_panic:
		ret = sys_panic(curenv->env_tf.regs[10]);
		break;
	case SYS_ipc_try_send:
		ret = sys_ipc_try_send(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                       curenv->env_tf.regs[12], curenv->env_tf.regs[13]);
		break;
	case SYS_ipc_recv:
		ret = sys_ipc_recv(curenv->env_tf.regs[10]);
		if (ret == 0) {
			return;
		}
		break;
	case SYS_ipc_info:
		ret = sys_ipc_info(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_cgetc:
		ret = sys_cgetc();
		break;
	case SYS_write_dev:
		ret = sys_write_dev(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                    curenv->env_tf.regs[12]);
		break;
	case SYS_read_dev:
		ret = sys_read_dev(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                   curenv->env_tf.regs[12]);
		break;
	case SYS_fs_open:
		ret = sys_fs_open(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_read:
		ret = sys_fs_read(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                  curenv->env_tf.regs[12]);
		break;
	case SYS_fs_close:
		ret = sys_fs_close(curenv->env_tf.regs[10]);
		break;
	case SYS_fs_stat:
		ret = sys_fs_stat(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_seek:
		ret = sys_fs_seek(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_write:
		ret = sys_fs_write(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                   curenv->env_tf.regs[12]);
		break;
	case SYS_spawn:
		ret = sys_spawn(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_unlink:
		ret = sys_fs_unlink(curenv->env_tf.regs[10]);
		break;
	case SYS_fs_rename:
		ret = sys_fs_rename(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_list:
		ret = sys_fs_list(curenv->env_tf.regs[10], curenv->env_tf.regs[11],
		                  curenv->env_tf.regs[12], curenv->env_tf.regs[13]);
		break;
	case SYS_fork:
		ret = sys_fork();
		break;
	case SYS_page_info:
		ret = sys_page_info(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_page_next:
		ret = sys_page_next(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_env_status:
		ret = sys_env_status(curenv->env_tf.regs[10]);
		break;
	case SYS_env_find:
		ret = sys_env_find(curenv->env_tf.regs[10]);
		break;
	case SYS_block_read:
		ret = sys_block_read(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_block_write:
		ret = sys_block_write(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_chmod:
		ret = sys_fs_chmod(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_fstat:
		ret = sys_fs_fstat(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_dup:
		ret = sys_fs_dup(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_fs_truncate:
		ret = sys_fs_truncate(curenv->env_tf.regs[10], curenv->env_tf.regs[11]);
		break;
	case SYS_pipe:
		ret = sys_pipe(curenv->env_tf.regs[10]);
		break;
	case SYS_fs_mkdir:
		ret = sys_fs_mkdir(curenv->env_tf.regs[10]);
		break;
	case SYS_fs_sync:
		ret = sys_fs_sync();
		break;
	default:
		ret = -E_NO_SYS;
		break;
	}

	curenv->env_tf.regs[10] = (reg_t)ret;
	schedule(0);
}
