#include <arch/csr.h>
#include <env.h>
#include <error.h>
#include <pmap.h>
#include <string.h>

extern char kernel_end[];
extern char text_start[];
extern char text_end[];
extern char rodata_start[];
extern char rodata_end[];
extern char data_start[];
extern char data_end[];
extern char bss_start[];
extern char bss_end[];

u_long npage;
paddr_t maxpa;
struct Page *pages;
struct Page_list page_free_list;
pagetable_t kernel_pagetable;

static uintptr_t freemem;

static uint64_t root_leaf_perms(void) {
	return PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D;
}

static uint64_t selfmap_leaf_perms(void) {
	return PTE_V | PTE_R | PTE_U | PTE_A;
}

static int pa_has_page(paddr_t pa) {
	return pa >= DRAM_BASE && pa < maxpa && (pa & (PAGE_SIZE - 1)) == 0;
}

void detect_memory(void) {
	maxpa = DRAM_BASE + PHYS_MEMORY_SIZE;
	npage = PHYS_MEMORY_SIZE / PAGE_SIZE;
}

void *boot_alloc(size_t n, size_t align, int clear) {
	uintptr_t alloced_mem;

	if (freemem == 0) {
		freemem = ROUND(kernel_end, PAGE_SIZE);
	}

	freemem = ROUND(freemem, align);
	alloced_mem = freemem;
	freemem += n;

	if ((paddr_t)freemem > maxpa) {
		panic("boot_alloc ran out of memory at %p", (void *)alloced_mem);
	}

	if (clear) {
		memset((void *)alloced_mem, 0, n);
	}
	return (void *)alloced_mem;
}

void page_init(void) {
	LIST_INIT(&page_free_list);
	freemem = ROUND(freemem, PAGE_SIZE);

	paddr_t used_end = (paddr_t)freemem;
	for (long i = (long)npage - 1; i >= 0; i--) {
		struct Page *pp = &pages[i];
		paddr_t pa = DRAM_BASE + ((paddr_t)i << PAGE_SHIFT);
		if (pa < used_end) {
			pp->pp_ref = 1;
		} else {
			pp->pp_ref = 0;
			LIST_INSERT_HEAD(&page_free_list, pp, pp_link);
		}
	}
}

int page_alloc(struct Page **pp_store) {
	struct Page *pp = LIST_FIRST(&page_free_list);
	if (pp == NULL) {
		return -E_NO_MEM;
	}

	LIST_REMOVE(pp, pp_link);
	memset(page2kva(pp), 0, PAGE_SIZE);
	*pp_store = pp;
	return 0;
}

void page_free(struct Page *pp) {
	assert(pp->pp_ref == 0);
	LIST_INSERT_HEAD(&page_free_list, pp, pp_link);
}

void page_decref(struct Page *pp) {
	assert(pp->pp_ref > 0);
	pp->pp_ref--;
	if (pp->pp_ref == 0) {
		page_free(pp);
	}
}

static int set_selfmap_leaf(pte_t *pte, struct Page *pp) {
	pte_t new_pte = PA2PTE(page2pa(pp)) | selfmap_leaf_perms();

	if ((*pte & PTE_V) && PTE_IS_LEAF(*pte)) {
		if (PTE2PA(*pte) == page2pa(pp)) {
			*pte = new_pte;
			return 0;
		}
		page_decref(pa2page(PTE2PA(*pte)));
	}
	pp->pp_ref++;
	*pte = new_pte;
	return 0;
}

static int selfmap_table_ready(pagetable_t root, pagetable_t *uvpt_l0_store) {
	pagetable_t l1;
	pte_t pte;

	if (root == NULL || !(root[PX(2, 0)] & PTE_V) || PTE_IS_LEAF(root[PX(2, 0)])) {
		return 0;
	}
	l1 = (pagetable_t)pa2kva(PTE2PA(root[PX(2, 0)]));
	pte = l1[PX(1, UVPT)];
	if (!(pte & PTE_V) || PTE_IS_LEAF(pte)) {
		return 0;
	}
	if (uvpt_l0_store != NULL) {
		*uvpt_l0_store = (pagetable_t)pa2kva(PTE2PA(pte));
	}
	return 1;
}

static int mirror_l0_to_uvpt(pagetable_t root, uint64_t vpn1, struct Page *l0_page) {
	pagetable_t uvpt_l0;

	if (vpn1 >= PT_ENTRIES || !selfmap_table_ready(root, &uvpt_l0)) {
		return 0;
	}
	return set_selfmap_leaf(&uvpt_l0[vpn1], l0_page);
}

static int alloc_table_page(struct Page **pp_store) {
	struct Page *pp;

	try(page_alloc(&pp));
	pp->pp_ref++;
	*pp_store = pp;
	return 0;
}

int pt_walk(pagetable_t root, vaddr_t va, int create, pte_t **pte_store) {
	if (!sv39_is_canonical(va)) {
		return -E_INVAL;
	}

	pagetable_t table = root;
	for (int level = PT_LEVELS - 1; level > 0; level--) {
		pte_t *entry = &table[PX(level, va)];
		if (!(*entry & PTE_V)) {
			if (!create) {
				*pte_store = NULL;
				return 0;
			}
			struct Page *new_page;
			try(alloc_table_page(&new_page));
			*entry = PA2PTE(page2pa(new_page)) | PTE_V;
			if (level == 1 && PX(2, va) == PX(2, 0)) {
				try(mirror_l0_to_uvpt(root, PX(1, va), new_page));
			}
		}
		if (PTE_IS_LEAF(*entry)) {
			return -E_INVAL;
		}
		table = (pagetable_t)pa2kva(PTE2PA(*entry));
	}

	*pte_store = &table[PX(0, va)];
	return 0;
}

struct Page *page_lookup(pagetable_t root, vaddr_t va, pte_t **pte_store) {
	pte_t *pte;
	if (pt_walk(root, va, 0, &pte) != 0 || pte == NULL || !(*pte & PTE_V) || !PTE_IS_LEAF(*pte)) {
		if (pte_store) {
			*pte_store = pte;
		}
		return NULL;
	}
	if (pte_store) {
		*pte_store = pte;
	}
	return pa2page(PTE2PA(*pte));
}

void page_remove(pagetable_t root, vaddr_t va) {
	pte_t *pte;
	struct Page *pp = page_lookup(root, va, &pte);
	if (pp == NULL) {
		return;
	}
	*pte = 0;
	page_decref(pp);
	asm volatile("sfence.vma %0, x0" :: "r"(va) : "memory");
}

static void vm_free_level(pagetable_t table, int level, vaddr_t base, vaddr_t limit) {
	for (int i = 0; i < (int)PT_ENTRIES; i++) {
		pte_t pte = table[i];
		vaddr_t va = base + ((vaddr_t)i << (PAGE_SHIFT + 9 * level));
		vaddr_t span = (vaddr_t)1 << (PAGE_SHIFT + 9 * level);

		if (va >= limit) {
			break;
		}
		if (!(pte & PTE_V)) {
			continue;
		}
		if (PTE_IS_LEAF(pte)) {
			paddr_t pa = PTE2PA(pte);

			table[i] = 0;
			if ((pte & PTE_U) && pa_has_page(pa)) {
				page_decref(pa2page(pa));
			}
			continue;
		}
		if (level > 0) {
			pagetable_t child = (pagetable_t)pa2kva(PTE2PA(pte));
			struct Page *child_page = pa2page(PTE2PA(pte));

			vm_free_level(child, level - 1, va, limit < va + span ? limit : va + span);
			table[i] = 0;
			page_decref(child_page);
		}
	}
}

void vm_free_user_pagetable(pagetable_t root, vaddr_t limit) {
	if (root == NULL) {
		return;
	}
	vm_free_level(root, PT_LEVELS - 1, 0, limit);
	asm volatile("sfence.vma x0, x0" ::: "memory");
}

int vm_install_user_selfmap(pagetable_t root) {
	struct Page *l1_page;
	struct Page *uvpd_l0_page;
	struct Page *uvpt_l0_page;
	pagetable_t l1;
	pagetable_t uvpd_l0;
	pagetable_t uvpt_l0;
	pte_t *entry;

	if (root == NULL) {
		return -E_INVAL;
	}

	entry = &root[PX(2, 0)];
	if (!(*entry & PTE_V)) {
		try(alloc_table_page(&l1_page));
		*entry = PA2PTE(page2pa(l1_page)) | PTE_V;
	} else if (PTE_IS_LEAF(*entry)) {
		return -E_INVAL;
	} else {
		l1_page = pa2page(PTE2PA(*entry));
	}
	l1 = (pagetable_t)page2kva(l1_page);

	entry = &l1[PX(1, UVPD)];
	if (!(*entry & PTE_V)) {
		try(alloc_table_page(&uvpd_l0_page));
		*entry = PA2PTE(page2pa(uvpd_l0_page)) | PTE_V;
	} else if (PTE_IS_LEAF(*entry)) {
		return -E_INVAL;
	} else {
		uvpd_l0_page = pa2page(PTE2PA(*entry));
	}
	uvpd_l0 = (pagetable_t)page2kva(uvpd_l0_page);

	entry = &l1[PX(1, UVPT)];
	if (!(*entry & PTE_V)) {
		try(alloc_table_page(&uvpt_l0_page));
		*entry = PA2PTE(page2pa(uvpt_l0_page)) | PTE_V;
	} else if (PTE_IS_LEAF(*entry)) {
		return -E_INVAL;
	} else {
		uvpt_l0_page = pa2page(PTE2PA(*entry));
	}
	uvpt_l0 = (pagetable_t)page2kva(uvpt_l0_page);

	try(set_selfmap_leaf(&uvpd_l0[PX(0, UVPD)], l1_page));
	try(set_selfmap_leaf(&uvpt_l0[PX(1, UVPD)], uvpd_l0_page));
	try(set_selfmap_leaf(&uvpt_l0[PX(1, UVPT)], uvpt_l0_page));

	for (uint64_t i = 0; i < PT_ENTRIES; i++) {
		pte_t pte = l1[i];

		if ((pte & PTE_V) && !PTE_IS_LEAF(pte)) {
			try(set_selfmap_leaf(&uvpt_l0[i], pa2page(PTE2PA(pte))));
		}
	}
	return 0;
}

static int vm_copy_user_cow_level(pagetable_t src, pagetable_t dst, int level,
                                  vaddr_t base, vaddr_t limit) {
	for (int i = 0; i < (int)PT_ENTRIES; i++) {
		pte_t *spte = &src[i];
		pte_t pte = *spte;
		vaddr_t va = base + ((vaddr_t)i << (PAGE_SHIFT + 9 * level));
		vaddr_t span = (vaddr_t)1 << (PAGE_SHIFT + 9 * level);

		if (va >= limit) {
			break;
		}
		if (!(pte & PTE_V)) {
			continue;
		}
		if (PTE_IS_LEAF(pte)) {
			struct Page *pp;
			uint64_t perm;

			if (!(pte & PTE_U)) {
				continue;
			}
			if (level != 0) {
				return -E_INVAL;
			}
			pp = pa2page(PTE2PA(pte));
			perm = PTE_FLAGS(pte);
			if (!(perm & PTE_LIBRARY) && (perm & (PTE_W | PTE_COW))) {
				perm = (perm & ~(PTE_W | PTE_D)) | PTE_COW;
				*spte = PA2PTE(page2pa(pp)) | perm | PTE_V;
			}
			try(page_insert(dst, pp, va, perm));
			continue;
		}
		try(vm_copy_user_cow_level((pagetable_t)pa2kva(PTE2PA(pte)), dst,
		                           level - 1, va,
		                           limit < va + span ? limit : va + span));
	}
	return 0;
}

int vm_copy_user_cow(pagetable_t src, pagetable_t dst, vaddr_t limit) {
	if (src == NULL || dst == NULL) {
		return -E_INVAL;
	}
	try(vm_copy_user_cow_level(src, dst, PT_LEVELS - 1, 0, limit));
	asm volatile("sfence.vma x0, x0" ::: "memory");
	return 0;
}

int vm_handle_cow_fault(pagetable_t root, vaddr_t fault_va) {
	vaddr_t va = ROUNDDOWN(fault_va, PAGE_SIZE);
	pte_t *pte;
	struct Page *old_page;
	struct Page *new_page;
	uint64_t perm;

	if (root == NULL || va >= USER_TOP) {
		return -E_INVAL;
	}
	try(pt_walk(root, va, 0, &pte));
	if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_COW) || !PTE_IS_LEAF(*pte)) {
		return -E_INVAL;
	}
	old_page = pa2page(PTE2PA(*pte));
	perm = (PTE_FLAGS(*pte) | PTE_W | PTE_D) & ~PTE_COW;
	if (old_page->pp_ref == 1) {
		*pte = PA2PTE(page2pa(old_page)) | perm | PTE_V;
		asm volatile("sfence.vma %0, x0" :: "r"(va) : "memory");
		return 0;
	}
	try(page_alloc(&new_page));
	memcpy(page2kva(new_page), page2kva(old_page), PAGE_SIZE);
	page_remove(root, va);
	return page_insert(root, new_page, va, perm);
}

int page_insert(pagetable_t root, struct Page *pp, vaddr_t va, uint64_t perm) {
	pte_t *pte;
	struct Page *old_pp;
	try(pt_walk(root, va, 1, &pte));

	if ((*pte & PTE_V) && PTE_IS_LEAF(*pte)) {
		old_pp = pa2page(PTE2PA(*pte));
		if (old_pp != pp) {
			page_remove(root, va);
		} else {
			*pte = PA2PTE(page2pa(pp)) | perm | PTE_V;
			asm volatile("sfence.vma %0, x0" :: "r"(va) : "memory");
			return 0;
		}
	}

	pp->pp_ref++;
	*pte = PA2PTE(page2pa(pp)) | perm | PTE_V;
	asm volatile("sfence.vma %0, x0" :: "r"(va) : "memory");
	return 0;
}

int translate(pagetable_t root, vaddr_t va, paddr_t *pa_out, pte_t **pte_out) {
	pte_t *pte;
	struct Page *pp = page_lookup(root, va, &pte);
	if (pp == NULL) {
		return -E_INVAL;
	}
	if (pa_out) {
		*pa_out = page2pa(pp) + (va & (PAGE_SIZE - 1));
	}
	if (pte_out) {
		*pte_out = pte;
	}
	return 0;
}

int page_query(pagetable_t root, vaddr_t va, struct UserPageInfo *info) {
	pte_t *pte;
	struct Page *pp;
	vaddr_t page_va = ROUNDDOWN(va, PAGE_SIZE);

	if (info == NULL || root == NULL) {
		return -E_INVAL;
	}
	memset(info, 0, sizeof(*info));
	info->va = page_va;
	if (pt_walk(root, page_va, 0, &pte) < 0 || pte == NULL ||
	    !(*pte & PTE_V) || !PTE_IS_LEAF(*pte)) {
		return 0;
	}
	pp = pa2page(PTE2PA(*pte));
	info->present = 1;
	info->pa = page2pa(pp);
	info->perm = PTE_FLAGS(*pte);
	info->ref = pp->pp_ref;
	return 0;
}

int page_next_mapped(pagetable_t root, vaddr_t start, vaddr_t limit, struct UserPageInfo *info) {
	vaddr_t va;

	if (root == NULL || info == NULL || start >= limit) {
		return 0;
	}
	va = ROUNDDOWN(start, PAGE_SIZE);
	for (; va < limit; va += PAGE_SIZE) {
		int r = page_query(root, va, info);

		if (r < 0) {
			return r;
		}
		if (info->present) {
			return 1;
		}
	}
	return 0;
}

int page_set_perm(pagetable_t root, vaddr_t va, uint64_t perm) {
	pte_t *pte;
	struct Page *pp;
	vaddr_t page_va = ROUNDDOWN(va, PAGE_SIZE);

	if (root == NULL) {
		return -E_INVAL;
	}
	pp = page_lookup(root, page_va, &pte);
	if (pp == NULL) {
		return -E_INVAL;
	}
	*pte = PA2PTE(page2pa(pp)) | perm | PTE_V;
	asm volatile("sfence.vma %0, x0" :: "r"(page_va) : "memory");
	return 0;
}

static void map_root_gigapage(pagetable_t root, vaddr_t va, paddr_t pa, uint64_t perm) {
	root[PX(2, va)] = PA2PTE(pa) | perm;
}

static int map_kernel_2m(pagetable_t root, vaddr_t va, paddr_t pa, uint64_t perm) {
	struct Page *l1_page;
	pagetable_t l1;
	pte_t *entry;

	if ((va & (SV39_LARGE_PAGE_SIZE - 1)) != 0 ||
	    (pa & (SV39_LARGE_PAGE_SIZE - 1)) != 0) {
		return -E_INVAL;
	}
	entry = &root[PX(2, va)];
	if (!(*entry & PTE_V)) {
		try(alloc_table_page(&l1_page));
		*entry = PA2PTE(page2pa(l1_page)) | PTE_V;
	} else if (PTE_IS_LEAF(*entry)) {
		return -E_INVAL;
	}
	l1 = (pagetable_t)pa2kva(PTE2PA(*entry));
	l1[PX(1, va)] = PA2PTE(pa) | perm | PTE_V;
	return 0;
}

void vm_bootstrap(void) {
	detect_memory();
	pages = (struct Page *)boot_alloc(npage * sizeof(struct Page), PAGE_SIZE, 1);
	kernel_pagetable = (pagetable_t)boot_alloc(PAGE_SIZE, PAGE_SIZE, 1);
	page_init();

	map_root_gigapage(kernel_pagetable, DRAM_BASE, DRAM_BASE, root_leaf_perms());
	panic_on(vm_map_kernel_devices(kernel_pagetable));
}

int vm_map_kernel_devices(pagetable_t root) {
	uint64_t perm = PTE_R | PTE_W | PTE_G | PTE_A | PTE_D;

	try(map_kernel_2m(root, CLINT_BASE, CLINT_BASE, perm));
	try(map_kernel_2m(root, PLIC_BASE, PLIC_BASE, perm));
	try(map_kernel_2m(root, PLIC_BASE + SV39_LARGE_PAGE_SIZE,
	                  PLIC_BASE + SV39_LARGE_PAGE_SIZE, perm));
	try(map_kernel_2m(root, UART0_BASE, UART0_BASE, perm));
	return 0;
}

void vm_enable(void) {
	uint64_t satp = SATP_MODE_SV39 | (kva2pa(kernel_pagetable) >> PAGE_SHIFT);
	csr_write_satp(satp);
	asm volatile("sfence.vma x0, x0" ::: "memory");
}

void vm_self_test(void) {
	struct Page *pp;
	pte_t *pte;
	paddr_t pa;
	vaddr_t test_va = 0x400000UL;

	panic_on(page_alloc(&pp));
	assert(pp->pp_ref == 0);
	panic_on(page_insert(kernel_pagetable, pp, test_va,
			     PTE_R | PTE_W | PTE_A | PTE_D | PTE_G));
	assert(pp->pp_ref == 1);
	panic_on(translate(kernel_pagetable, test_va, &pa, &pte));
	assert(pa == page2pa(pp));
	assert((*pte & (PTE_R | PTE_W)) == (PTE_R | PTE_W));
	page_remove(kernel_pagetable, test_va);
	assert(pp->pp_ref == 0);
}
