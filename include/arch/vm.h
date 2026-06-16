#ifndef _MOS_RISCV_ARCH_VM_H_
#define _MOS_RISCV_ARCH_VM_H_

#include <arch/riscv.h>

#define DRAM_BASE 0x80000000UL
#define PHYS_MEMORY_SIZE (2UL * 1024 * 1024 * 1024)

#define SATP_MODE_SV39 (8UL << 60)

#define PT_ENTRIES 512UL
#define PT_LEVELS 3
#define PT_INDEX_MASK 0x1ffUL

#define PTE_V (1UL << 0)
#define PTE_R (1UL << 1)
#define PTE_W (1UL << 2)
#define PTE_X (1UL << 3)
#define PTE_U (1UL << 4)
#define PTE_G (1UL << 5)
#define PTE_A (1UL << 6)
#define PTE_D (1UL << 7)
#define PTE_COW (1UL << 8)
#define PTE_LIBRARY (1UL << 9)

#define PTE_FLAGS_MASK 0x3ffUL
#define PTE_PPN_SHIFT 10

#define SV39_LARGE_PAGE_SIZE (2UL * 1024 * 1024)
#define SV39_ROOT_SPAN (1UL * 1024 * 1024 * 1024)

#define PX(level, va) (((uint64_t)(va) >> (PAGE_SHIFT + 9 * (level))) & PT_INDEX_MASK)
#define PTE_FLAGS(pte) ((pte) & PTE_FLAGS_MASK)
#define PTE2PA(pte) (((pte) >> PTE_PPN_SHIFT) << PAGE_SHIFT)
#define PA2PTE(pa) ((((uint64_t)(pa)) >> PAGE_SHIFT) << PTE_PPN_SHIFT)
#define PTE_IS_LEAF(pte) ((pte) & (PTE_R | PTE_W | PTE_X))

static inline int sv39_is_canonical(vaddr_t va) {
	uint64_t top = va >> 39;
	return top == 0 || top == ((1UL << 25) - 1);
}

#endif
