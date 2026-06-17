#include <arch/trap.h>
#include <arch/vm.h>
#include <env.h>
#include <error.h>

#include "lib.h"

static void user_memcpy(void *dst, const void *src, u_long n) {
	char *d = dst;
	const char *s = src;

	for (u_long i = 0; i < n; i++) {
		d[i] = s[i];
	}
}

static void cow_fault_entry(struct Trapframe *tf) {
	void *page_va = (void *)ROUNDDOWN(tf->stval, PAGE_SIZE);
	uint64_t pte = VPT[VPN(page_va)];
	u_long perm;

	if (tf->scause != SCAUSE_STORE_PAGE_FAULT ||
	    !(pte & PTE_V) || !(pte & PTE_COW)) {
		syscall_panic("bad user cow fault");
	}

	perm = (PTE_FLAGS(pte) | PTE_D) & ~PTE_COW;
	if (syscall_mem_alloc(0, (void *)UCOW, perm) < 0) {
		syscall_panic("cow alloc failed");
	}
	user_memcpy((void *)UCOW, page_va, PAGE_SIZE);
	if (syscall_mem_map(0, (void *)UCOW, 0, page_va, perm) < 0) {
		syscall_panic("cow remap failed");
	}
	if (syscall_mem_unmap(0, (void *)UCOW) < 0) {
		syscall_panic("cow unmap failed");
	}
	if (syscall_set_trapframe(0, tf) < 0) {
		syscall_panic("cow restore failed");
	}
	syscall_panic("cow handler returned");
}

static int duppage(u_long child, u_long va) {
	uint64_t pte;
	u_long perm;
	void *addr = (void *)va;

	pte = VPT[VPN(va)];
	if (!(pte & PTE_V)) {
		return 0;
	}
	perm = PTE_FLAGS(pte);
	if (perm & PTE_LIBRARY) {
		return syscall_mem_map(0, addr, child, addr, perm);
	}
	if (perm & (PTE_D | PTE_W | PTE_COW)) {
		perm = (perm | PTE_COW) & ~(PTE_D | PTE_W);
		if (syscall_mem_map(0, addr, child, addr, perm) < 0) {
			return -1;
		}
		return syscall_mem_protect(0, addr, perm);
	}
	return syscall_mem_map(0, addr, child, addr, perm);
}

int fork_user(void) {
	int child;

	if (syscall_set_tlb_mod_entry(0, cow_fault_entry) < 0) {
		return -1;
	}

	child = syscall_exofork();
	if (child < 0) {
		return child;
	}
	if (child == 0) {
		return 0;
	}

	for (u_long va = UTEMP; va < USER_MAP_TOP; va += PAGE_SIZE) {
		if (!(VPD[PX(1, va)] & PTE_V) || !(VPD[PX(1, va)] & PTE_U) ||
		    !(VPT[VPN(va)] & PTE_V)) {
			continue;
		}
		if (va == UCOW || va == UXSTACKBASE) {
			continue;
		}
		if (duppage((u_long)child, va) < 0) {
			(void)syscall_env_destroy((u_long)child);
			return -1;
		}
	}
	if (syscall_mem_alloc((u_long)child, (void *)UXSTACKBASE, PTE_D) < 0) {
		(void)syscall_env_destroy((u_long)child);
		return -1;
	}
	if (syscall_set_tlb_mod_entry((u_long)child, cow_fault_entry) < 0 ||
	    syscall_set_env_status((u_long)child, ENV_RUNNABLE) < 0) {
		(void)syscall_env_destroy((u_long)child);
		return -1;
	}
	return child;
}
