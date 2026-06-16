#include <arch/csr.h>
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
	for (u_long i = 0; i < npage; i++) {
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
			try(page_alloc(&new_page));
			new_page->pp_ref++;
			*entry = PA2PTE(page2pa(new_page)) | PTE_V;
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

static void map_root_gigapage(pagetable_t root, vaddr_t va, paddr_t pa, uint64_t perm) {
	root[PX(2, va)] = PA2PTE(pa) | perm;
}

void vm_bootstrap(void) {
	detect_memory();
	pages = (struct Page *)boot_alloc(npage * sizeof(struct Page), PAGE_SIZE, 1);
	kernel_pagetable = (pagetable_t)boot_alloc(PAGE_SIZE, PAGE_SIZE, 1);
	page_init();

	map_root_gigapage(kernel_pagetable, DRAM_BASE, DRAM_BASE, root_leaf_perms());
}

void vm_enable(void) {
	uint64_t satp = SATP_MODE_SV39 | (kva2pa(kernel_pagetable) >> PAGE_SHIFT);
	asm volatile("csrw satp, %0" :: "r"(satp) : "memory");
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
