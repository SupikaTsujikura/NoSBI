#ifndef _MOS_RISCV_ERROR_H_
#define _MOS_RISCV_ERROR_H_

#define E_SUCCESS 0
#define E_UNSPECIFIED 1
#define E_BAD_ENV 2
#define E_INVAL 3
#define E_NO_MEM 4
#define E_NO_SYS 5
#define E_NO_FREE_ENV 6
#define E_IPC_NOT_RECV 7

#define try(expr)                                                                                  \
	do {                                                                                         \
		int _r = (expr);                                                                     \
		if (_r != 0)                                                                         \
			return _r;                                                                     \
	} while (0)

#endif
