#include <syscall.h>
#include "lib.h"

long msyscall(long sysno, long a0, long a1, long a2, long a3, long a4, long a5) {
	register long ra0 asm("a0") = a0;
	register long ra1 asm("a1") = a1;
	register long ra2 asm("a2") = a2;
	register long ra3 asm("a3") = a3;
	register long ra4 asm("a4") = a4;
	register long ra5 asm("a5") = a5;
	register long ra7 asm("a7") = sysno;

	asm volatile("ecall"
	             : "+r"(ra0)
	             : "r"(ra1), "r"(ra2), "r"(ra3), "r"(ra4), "r"(ra5), "r"(ra7)
	             : "memory");
	return ra0;
}

void syscall_putchar(int ch) {
	(void)msyscall(SYS_putchar, ch, 0, 0, 0, 0, 0);
}

int syscall_print_cons(const char *str, u_long len) {
	return (int)msyscall(SYS_print_cons, (long)str, (long)len, 0, 0, 0, 0);
}

u_long syscall_getenvid(void) {
	return (u_long)msyscall(SYS_getenvid, 0, 0, 0, 0, 0, 0);
}

void syscall_yield(void) {
	(void)msyscall(SYS_yield, 0, 0, 0, 0, 0, 0);
}

int syscall_env_destroy(u_long envid) {
	return (int)msyscall(SYS_env_destroy, (long)envid, 0, 0, 0, 0, 0);
}

int syscall_mem_alloc(u_long envid, void *va, u_long perm) {
	return (int)msyscall(SYS_mem_alloc, (long)envid, (long)va, (long)perm, 0, 0, 0);
}

int syscall_mem_map(u_long srcid, void *srcva, u_long dstid, void *dstva, u_long perm) {
	return (int)msyscall(SYS_mem_map, (long)srcid, (long)srcva, (long)dstid,
	                     (long)dstva, (long)perm, 0);
}

int syscall_mem_unmap(u_long envid, void *va) {
	return (int)msyscall(SYS_mem_unmap, (long)envid, (long)va, 0, 0, 0, 0);
}

int syscall_mem_protect(u_long envid, void *va, u_long perm) {
	return (int)msyscall(SYS_mem_protect, (long)envid, (long)va, (long)perm, 0, 0, 0);
}

int syscall_page_info(void *va, struct UserPageInfo *info) {
	return (int)msyscall(SYS_page_info, (long)va, (long)info, 0, 0, 0, 0);
}

int syscall_page_next(void *start, struct UserPageInfo *info) {
	return (int)msyscall(SYS_page_next, (long)start, (long)info, 0, 0, 0, 0);
}

int syscall_exofork(void) {
	return (int)msyscall(SYS_exofork, 0, 0, 0, 0, 0, 0);
}

int syscall_set_env_status(u_long envid, u_long status) {
	return (int)msyscall(SYS_env_set_status, (long)envid, (long)status, 0, 0, 0, 0);
}

int syscall_set_tlb_mod_entry(u_long envid, void (*func)(struct Trapframe *tf)) {
	return (int)msyscall(SYS_set_tlb_mod_entry, (long)envid, (long)func, 0, 0, 0, 0);
}

int syscall_set_trapframe(u_long envid, const struct Trapframe *tf) {
	return (int)msyscall(SYS_set_trapframe, (long)envid, (long)tf, 0, 0, 0, 0);
}

void syscall_panic(const char *msg) {
	(void)msyscall(SYS_panic, (long)msg, 0, 0, 0, 0, 0);
	for (;;) {
		syscall_yield();
	}
}

int syscall_ipc_try_send(u_long envid, u_long value, const void *srcva, u_long perm) {
	return (int)msyscall(SYS_ipc_try_send, (long)envid, (long)value, (long)srcva,
	                     (long)perm, 0, 0);
}

long syscall_ipc_recv(void *dstva) {
	return msyscall(SYS_ipc_recv, (long)dstva, 0, 0, 0, 0, 0);
}

int syscall_ipc_info(u_long *from, u_long *perm) {
	return (int)msyscall(SYS_ipc_info, (long)from, (long)perm, 0, 0, 0, 0);
}

int syscall_fs_open(const char *path) {
	return (int)msyscall(SYS_fs_open, (long)path, 0, 0, 0, 0, 0);
}

int syscall_fs_open_flags(const char *path, int flags) {
	return (int)msyscall(SYS_fs_open, (long)path, flags, 0, 0, 0, 0);
}

int syscall_fs_read(int fd, void *buf, u_long len) {
	return (int)msyscall(SYS_fs_read, fd, (long)buf, (long)len, 0, 0, 0);
}

int syscall_fs_write(int fd, const void *buf, u_long len) {
	return (int)msyscall(SYS_fs_write, fd, (long)buf, (long)len, 0, 0, 0);
}

int syscall_fs_close(int fd) {
	return (int)msyscall(SYS_fs_close, fd, 0, 0, 0, 0, 0);
}

int syscall_fs_stat(const char *path, struct FsStat *stat) {
	return (int)msyscall(SYS_fs_stat, (long)path, (long)stat, 0, 0, 0, 0);
}

int syscall_fs_seek(int fd, u_long off) {
	return (int)msyscall(SYS_fs_seek, fd, (long)off, 0, 0, 0, 0);
}

int syscall_spawn(const char *path, long arg) {
	return (int)msyscall(SYS_spawn, (long)path, arg, 0, 0, 0, 0);
}

int syscall_fs_unlink(const char *path) {
	return (int)msyscall(SYS_fs_unlink, (long)path, 0, 0, 0, 0, 0);
}

int syscall_fs_rename(const char *old_path, const char *new_path) {
	return (int)msyscall(SYS_fs_rename, (long)old_path, (long)new_path, 0, 0, 0, 0);
}

int syscall_fs_list(int index, char *path, u_long path_len, struct FsStat *stat) {
	return (int)msyscall(SYS_fs_list, index, (long)path, (long)path_len,
	                     (long)stat, 0, 0);
}

int syscall_fork(void) {
	return (int)msyscall(SYS_fork, 0, 0, 0, 0, 0, 0);
}

int syscall_env_status(u_long envid) {
	return (int)msyscall(SYS_env_status, (long)envid, 0, 0, 0, 0, 0);
}

int syscall_env_find(const char *name) {
	return (int)msyscall(SYS_env_find, (long)name, 0, 0, 0, 0, 0);
}

int syscall_fs_fstat(int fd, struct FsStat *stat) {
	return (int)msyscall(SYS_fs_fstat, fd, (long)stat, 0, 0, 0, 0);
}

int syscall_fs_dup(int oldfd, int newfd) {
	return (int)msyscall(SYS_fs_dup, oldfd, newfd, 0, 0, 0, 0);
}

int syscall_fs_truncate(int fd, u_long size) {
	return (int)msyscall(SYS_fs_truncate, fd, (long)size, 0, 0, 0, 0);
}

int syscall_write_dev(const void *src, u_long pa, u_long len) {
	return (int)msyscall(SYS_write_dev, (long)src, (long)pa, (long)len, 0, 0, 0);
}

int syscall_read_dev(void *dst, u_long pa, u_long len) {
	return (int)msyscall(SYS_read_dev, (long)dst, (long)pa, (long)len, 0, 0, 0);
}

int syscall_pipe(int pfd[2]) {
	return (int)msyscall(SYS_pipe, (long)pfd, 0, 0, 0, 0, 0);
}

int syscall_cgetc(void) {
	return (int)msyscall(SYS_cgetc, 0, 0, 0, 0, 0, 0);
}

int syscall_block_read(u_long sector, void *buf) {
	return (int)msyscall(SYS_block_read, (long)sector, (long)buf, 0, 0, 0, 0);
}

int syscall_block_write(u_long sector, const void *buf) {
	return (int)msyscall(SYS_block_write, (long)sector, (long)buf, 0, 0, 0, 0);
}

int syscall_fs_chmod(const char *path, u32 mode) {
	return (int)msyscall(SYS_fs_chmod, (long)path, (long)mode, 0, 0, 0, 0);
}

int syscall_fs_mkdir(const char *path) {
	return (int)msyscall(SYS_fs_mkdir, (long)path, 0, 0, 0, 0, 0);
}

int syscall_fs_sync(void) {
	return (int)msyscall(SYS_fs_sync, 0, 0, 0, 0, 0, 0);
}
