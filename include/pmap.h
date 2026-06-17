#ifndef _MOS_RISCV_PMAP_H_
#define _MOS_RISCV_PMAP_H_

#include <arch/vm.h>
#include <printk.h>
#include <queue.h>
#include <types.h>

LIST_HEAD(Page_list, Page);

typedef uint64_t pte_t;
typedef pte_t *pagetable_t;

struct Page {
	LIST_ENTRY(Page) pp_link;
	u_short pp_ref;
};

extern u_long npage;
extern paddr_t maxpa;
extern struct Page *pages;
extern struct Page_list page_free_list;
extern pagetable_t kernel_pagetable;

#define assert(x)                                                                                  \
	do {                                                                                         \
		if (!(x)) {                                                                              \
			panic("assertion failed: %s", #x);                                               \
		}                                                                                        \
	} while (0)

static inline u_long page2ppn(struct Page *pp) {
	return (u_long)(pp - pages);
}

static inline paddr_t page2pa(struct Page *pp) {
	return DRAM_BASE + ((paddr_t)page2ppn(pp) << PAGE_SHIFT);
}

static inline struct Page *pa2page(paddr_t pa) {
	if (pa < DRAM_BASE || pa >= maxpa || (pa & (PAGE_SIZE - 1)) != 0) {
		panic("pa2page called with invalid pa: %016lx", pa);
	}
	return &pages[(pa - DRAM_BASE) >> PAGE_SHIFT];
}

static inline void *pa2kva(paddr_t pa) {
	if (pa < DRAM_BASE || pa >= maxpa) {
		panic("pa2kva called with invalid pa: %016lx", pa);
	}
	return (void *)(uintptr_t)pa;
}

static inline paddr_t kva2pa(const void *kva) {
	paddr_t pa = (paddr_t)(uintptr_t)kva;
	if (pa < DRAM_BASE || pa >= maxpa) {
		panic("kva2pa called with invalid kva: %p", kva);
	}
	return pa;
}

static inline void *page2kva(struct Page *pp) {
	return pa2kva(page2pa(pp));
}

void detect_memory(void);
void *boot_alloc(size_t n, size_t align, int clear);
void vm_bootstrap(void);
int vm_map_kernel_devices(pagetable_t root);
void page_init(void);
int page_alloc(struct Page **pp);
void page_free(struct Page *pp);
void page_decref(struct Page *pp);
int pt_walk(pagetable_t root, vaddr_t va, int create, pte_t **pte_store);
int page_insert(pagetable_t root, struct Page *pp, vaddr_t va, uint64_t perm);
struct Page *page_lookup(pagetable_t root, vaddr_t va, pte_t **pte_store);
void page_remove(pagetable_t root, vaddr_t va);
int translate(pagetable_t root, vaddr_t va, paddr_t *pa_out, pte_t **pte_out);
int page_query(pagetable_t root, vaddr_t va, struct UserPageInfo *info);
int page_next_mapped(pagetable_t root, vaddr_t start, vaddr_t limit, struct UserPageInfo *info);
int page_set_perm(pagetable_t root, vaddr_t va, uint64_t perm);
void vm_free_user_pagetable(pagetable_t root, vaddr_t limit);
int vm_install_user_selfmap(pagetable_t root);
int vm_copy_user_cow(pagetable_t src, pagetable_t dst, vaddr_t limit);
int vm_handle_cow_fault(pagetable_t root, vaddr_t fault_va);
void vm_enable(void);
void vm_self_test(void);

#endif
