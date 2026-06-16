#ifndef _MOS_RISCV_SYSCALL_H_
#define _MOS_RISCV_SYSCALL_H_

enum {
	SYS_putchar = 0,
	SYS_print_cons = 1,
	SYS_getenvid = 2,
	SYS_yield = 3,
	SYS_env_destroy = 4,
	SYS_mem_alloc = 6,
	SYS_mem_map = 7,
	SYS_mem_unmap = 8,
	SYS_exofork = 9,
	SYS_env_set_status = 10,
	SYS_panic = 12,
	SYS_ipc_try_send = 13,
	SYS_ipc_recv = 14,
	SYS_cgetc = 15,
	SYS_write_dev = 16,
	SYS_read_dev = 17,
	MAX_SYSNO,
};

#endif
