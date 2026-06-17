#ifndef _MOS_RISCV_ARCH_CSR_H_
#define _MOS_RISCV_ARCH_CSR_H_

#include <types.h>

#define SSTATUS_SIE  (1UL << 1)
#define SSTATUS_SPIE (1UL << 5)
#define SSTATUS_SUM  (1UL << 18)
#define SSTATUS_SPP  (1UL << 8)

#define SIE_SSIE (1UL << 1)
#define SIE_STIE (1UL << 5)
#define SIE_SEIE (1UL << 9)

#define SIP_SSIP (1UL << 1)
#define SIP_STIP (1UL << 5)
#define SIP_SEIP (1UL << 9)

#define SCAUSE_INTERRUPT (1UL << 63)

static inline reg_t csr_read_satp(void) {
	reg_t value;
	asm volatile("csrr %0, satp" : "=r"(value));
	return value;
}

static inline void csr_write_satp(reg_t value) {
	asm volatile("csrw satp, %0" :: "r"(value) : "memory");
}

static inline reg_t csr_swap_satp(reg_t value) {
	reg_t old;
	asm volatile("csrrw %0, satp, %1" : "=r"(old) : "r"(value) : "memory");
	return old;
}

static inline reg_t csr_read_sstatus(void) {
	reg_t value;
	asm volatile("csrr %0, sstatus" : "=r"(value));
	return value;
}

static inline reg_t csr_read_scause(void) {
	reg_t value;
	asm volatile("csrr %0, scause" : "=r"(value));
	return value;
}

static inline reg_t csr_read_sepc(void) {
	reg_t value;
	asm volatile("csrr %0, sepc" : "=r"(value));
	return value;
}

static inline reg_t csr_read_stval(void) {
	reg_t value;
	asm volatile("csrr %0, stval" : "=r"(value));
	return value;
}

static inline reg_t csr_read_stvec(void) {
	reg_t value;
	asm volatile("csrr %0, stvec" : "=r"(value));
	return value;
}

static inline reg_t csr_read_time(void) {
	reg_t value;
	asm volatile("csrr %0, time" : "=r"(value));
	return value;
}

static inline void csr_write_stvec(reg_t value) {
	asm volatile("csrw stvec, %0" :: "r"(value));
}

static inline void csr_write_sie(reg_t value) {
	asm volatile("csrw sie, %0" :: "r"(value));
}

static inline reg_t csr_read_sie(void) {
	reg_t value;
	asm volatile("csrr %0, sie" : "=r"(value));
	return value;
}

static inline reg_t csr_read_sip(void) {
	reg_t value;
	asm volatile("csrr %0, sip" : "=r"(value));
	return value;
}

static inline void csr_write_sip(reg_t value) {
	asm volatile("csrw sip, %0" :: "r"(value));
}

static inline void csr_set_sstatus(reg_t value) {
	asm volatile("csrs sstatus, %0" :: "r"(value));
}

static inline void csr_clear_sstatus(reg_t value) {
	asm volatile("csrc sstatus, %0" :: "r"(value));
}

static inline reg_t csr_read_clear_sstatus(reg_t value) {
	reg_t old;

	asm volatile("csrrc %0, sstatus, %1" : "=r"(old) : "r"(value) : "memory");
	return old;
}

static inline void wfi(void) {
	asm volatile("wfi");
}

#endif
