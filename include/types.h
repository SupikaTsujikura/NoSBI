#ifndef _MOS_RISCV_TYPES_H_
#define _MOS_RISCV_TYPES_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;
typedef uint64_t reg_t;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MIN(_a, _b)                                                                                \
	({                                                                                           \
		typeof(_a) __a = (_a);                                                               \
		typeof(_b) __b = (_b);                                                               \
		__a <= __b ? __a : __b;                                                              \
	})

#define MAX(_a, _b)                                                                                \
	({                                                                                           \
		typeof(_a) __a = (_a);                                                               \
		typeof(_b) __b = (_b);                                                               \
		__a >= __b ? __a : __b;                                                              \
	})

#define ROUND(a, n) (((((uintptr_t)(a)) + (uintptr_t)(n) - 1)) & ~((uintptr_t)(n) - 1))
#define ROUNDDOWN(a, n) (((uintptr_t)(a)) & ~((uintptr_t)(n) - 1))

#endif
