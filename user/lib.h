#ifndef _MOS_RISCV_USER_LIB_H_
#define _MOS_RISCV_USER_LIB_H_

#include <arch/trap.h>
#include <arch/vm.h>
#include <fs.h>
#include <types.h>

long msyscall(long sysno, long a0, long a1, long a2, long a3, long a4, long a5);
void syscall_putchar(int ch);
int syscall_print_cons(const char *str, u_long len);
u_long syscall_getenvid(void);
void syscall_yield(void);
int syscall_env_destroy(u_long envid);
int syscall_mem_alloc(u_long envid, void *va, u_long perm);
int syscall_mem_map(u_long srcid, void *srcva, u_long dstid, void *dstva, u_long perm);
int syscall_mem_unmap(u_long envid, void *va);
int syscall_mem_protect(u_long envid, void *va, u_long perm);
int syscall_page_info(void *va, struct UserPageInfo *info);
int syscall_page_next(void *start, struct UserPageInfo *info);
int syscall_exofork(void);
int syscall_set_env_status(u_long envid, u_long status);
int syscall_set_tlb_mod_entry(u_long envid, void (*func)(struct Trapframe *tf));
int syscall_set_trapframe(u_long envid, const struct Trapframe *tf);
void syscall_panic(const char *msg) __attribute__((noreturn));
int syscall_ipc_try_send(u_long envid, u_long value, const void *srcva, u_long perm);
long syscall_ipc_recv(void *dstva);
int syscall_ipc_info(u_long *from, u_long *perm);
int syscall_fs_open(const char *path);
int syscall_fs_open_flags(const char *path, int flags);
int syscall_fs_read(int fd, void *buf, u_long len);
int syscall_fs_write(int fd, const void *buf, u_long len);
int syscall_fs_close(int fd);
int syscall_fs_stat(const char *path, struct FsStat *stat);
int syscall_fs_seek(int fd, u_long off);
int syscall_spawn(const char *path, long arg);
int syscall_fs_unlink(const char *path);
int syscall_fs_rename(const char *old_path, const char *new_path);
int syscall_fs_list(int index, char *path, u_long path_len, struct FsStat *stat);
int syscall_fork(void);
int syscall_env_status(u_long envid);
int syscall_fs_fstat(int fd, struct FsStat *stat);
int syscall_fs_dup(int oldfd, int newfd);
int syscall_fs_truncate(int fd, u_long size);
int syscall_write_dev(const void *src, u_long pa, u_long len);
int syscall_read_dev(void *dst, u_long pa, u_long len);
int syscall_pipe(int pfd[2]);
int fork_user(void);

struct Stat {
	u64 st_size;
	u32 st_type;
	u32 st_reserved;
};

int fork(void);
void exit(void) __attribute__((noreturn));
void wait(u_long envid);
int ipc_send(u_long whom, u_long val, const void *srcva, u_long perm);
int ipc_recv(u_long *whom, u_long *val, void *dstva, u_long *perm);
int pipe(int pfd[2]);
int open(const char *path, int flags);
int read(int fd, void *buf, u_long n);
int readn(int fd, void *buf, u_long n);
int write(int fd, const void *buf, u_long n);
int seek(int fd, u_long offset);
int stat(const char *path, struct Stat *stat);
int fstat(int fd, struct Stat *stat);
int close(int fd);
void close_all(void);
int dup(int oldfd, int newfd);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
int ftruncate(int fd, u_long size);
int sync(void);
int spawn(char *prog, char **argv);
int spawnl(char *prog, const char *arg0, ...);
int printf(const char *fmt, ...);
int fprintf(int fd, const char *fmt, ...);
int debugf(const char *fmt, ...);

#define user_panic(...)                                                                            \
	do {                                                                                         \
		debugf("user panic: " __VA_ARGS__);                                                  \
		debugf("\n");                                                                       \
		exit();                                                                              \
	} while (0)
#define user_halt() exit()
#define user_assert(x)                                                                             \
	do {                                                                                         \
		if (!(x)) {                                                                           \
			user_panic("assertion failed: %s", #x);                                      \
		}                                                                                    \
	} while (0)

void user_main(long arg, char **argv);

#endif
