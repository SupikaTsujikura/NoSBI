# NoSBI RISC-V 移植实现报告

## 目标

本项目基于课程 MOS 思路，将内核移植到 RISC-V64 QEMU virt 平台，运行于 OpenSBI 之后的 S-Mode。当前版本面向课程 RISC-V 移植任务与文件系统挑战任务，重点实现启动、Sv39 虚拟内存、异常中断、进程调度、COW fork、IPC、用户态文件服务接口与 EXT4 持久写回。

## 启动与特权级

- OpenSBI 在 M-Mode 完成平台初始化后跳转到内核入口。
- 内核运行于 S-Mode，使用 `stvec` 安装 trap 入口。
- 通过 SBI timer 设置时钟中断，使用 `sstatus/sie/scause/sepc/stval/satp` 等 CSR 管理执行环境。
- 内核启动后依次初始化物理页、内核页表、PLIC、VirtIO block、文件系统、进程表和调度器。

## 内存管理

- 物理内存按 4KiB 页管理，维护 `struct Page` 和引用计数。
- 使用 Sv39 三级页表，支持 `map/unmap/translate/walk`。
- 用户地址空间包含 ELF 装载区、用户栈、异常栈、文件映射区和 UVPT/UVPD 页表自映射窗口。
- 支持 COW fork：fork 时共享用户页并标记 COW，写页异常时复制页面并恢复写权限。
- 已实现用户态异常栈和用户 page fault 处理入口，可支撑用户态 COW 处理流程。

## Trap、中断与调度

- `ecall` 从 U-Mode 进入 S-Mode 后由 syscall 分发器处理。
- 支持 store/load/instruction page fault、timer interrupt、external interrupt。
- timer interrupt 驱动时间片轮转调度。
- PLIC 已支持初始化、enable、claim、complete。
- VirtIO block 请求默认使用 PLIC 外部中断完成路径；若中断等待超时，会回退到轮询以保证稳定性。

## 系统调用

已实现课程要求的核心 syscall：

- `SYS_putchar`
- `SYS_print_cons`
- `SYS_getenvid`
- `SYS_yield`
- `SYS_env_destroy`
- `SYS_mem_alloc`
- `SYS_mem_map`
- `SYS_mem_unmap`
- `SYS_exofork`
- `SYS_env_set_status`
- `SYS_panic`
- `SYS_ipc_try_send`
- `SYS_ipc_recv`
- `SYS_cgetc`
- `SYS_write_dev`
- `SYS_read_dev`

同时保留若干扩展 syscall，用于用户态 COW、trapframe 恢复、文件系统、spawn、fork、页表查询和块设备访问。课程文档中 syscall 5、11 标为未使用，本项目将其作为兼容扩展使用，分别用于用户态 page fault 入口和 trapframe 恢复。若评测严格要求 5、11 返回 `E_NO_SYS`，需要额外提供兼容开关。

边界语义已收紧：

- `SYS_putchar` 拒绝大于 `0x7f` 的字符。
- `SYS_mem_alloc/map/protect` 拒绝非法权限位和空权限。
- `SYS_mem_unmap` 对未映射页面返回 `E_INVAL`。
- IPC 共享页会检查源页和写权限。
- `SYS_read_dev/write_dev` 检查用户地址、长度幂次/对齐和精确设备 MMIO 区间。

## 进程与用户程序

- 支持 ELF 解析和用户程序加载。
- 支持 `exofork`、内核级 `fork`、用户态 COW fork、`spawn`、`wait` 风格流程。
- 内核启动后创建 `fsserv` 和 `init` 用户进程，由用户态 `init` 继续启动 demo/测试程序。
- 支持基础 fd 接口、pipe、IPC、用户态 printf/debug 输出。

## 文件系统

当前文件系统由三层组成：

- 内核 VirtIO block 驱动负责块设备读写。
- 内核 EXT4 层负责真实 rootfs 镜像解析与持久写回。
- 用户态 `fsserv` 暴露 MOS 风格 `fsipc` 接口，管理用户态 open file、缓存页映射、dirty/sync、目录枚举等语义。

已实现能力：

- 读取 EXT4 镜像文件和目录。
- 创建、删除、重命名文件和目录。
- 文件截断、追加、chmod、hardlink、symlink、readlink。
- 新 inode/block bitmap 分配与释放。
- 持久目录项创建/删除/重命名。
- inode、目录块、bitmap、group descriptor、superblock checksum 更新。
- `fsipc_map/dirty/sync/list/getdents` 等接口。

架构说明：

课程文档允许自行选择文件系统服务方法，不强制兼容 `fsipc_map`。本项目保留 MOS 风格用户态 `fsserv/fsipc` 接口，但 EXT4 磁盘格式主体仍在内核实现，由 `fsserv` 通过内核块/文件 syscall 访问。这样功能更集中、调试更稳定；答辩时需说明这与 23241036 的“用户态 FS server 直接管理磁盘格式”不同。

## 已验证结果

使用 Docker 内 RISC-V 交叉工具链验证：

- `make` 编译通过，启用 `-Wall -Wextra -Werror`。
- `make test` QEMU 快速回归通过。
- QEMU 输出包含文件系统、spawn、pipe、IPC、fork/COW、设备地址检查等 smoke marker，并最终输出：
  - `TEST PASS: bootstrap, vm, trap, timer, user mode, syscall, and scheduling verified`
  - `PASS: kernel self-test completed and QEMU exited cleanly`
- EXT4 写回曾使用 rootfs 副本验证，重启后文件内容、权限和 `e2fsck -fn` 均正常。

## 仍需验收的内容

- 课程组最终用户程序需要逐项运行并记录日志，这是提交前最重要的验收项。
- 如果评测严格检测 syscall 5、11 未使用语义，需要增加编译开关或兼容模式。
- VirtIO 已走中断完成路径，但仍是单请求同步队列，不是完整多请求 virtqueue。
- 文件系统已支持 EXT4 持久写回，但还不是 23241036 那种完全用户态磁盘格式服务；若挑战任务重点考察用户态动态缓存地址分配，应继续加强 `fsserv` 的 fd 页、文件数据窗口、缓存释放和低地址复用测试。

