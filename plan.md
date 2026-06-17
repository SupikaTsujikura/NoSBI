# RISC-V MOS Port Plan / Status

> 更新时间：当前仓库状态对应 Claude 已实现到“最小 U 态 + 最小 syscall + 定时器驱动调度”阶段。
>
> 本文件用于后续管理：明确**已经实现的闭环**、**部分实现但还需增强的模块**、**尚未实现的工作**。

---

## 一、当前已经实现并验证通过的功能

### 1. 内核启动闭环
已实现：
- RV64 独立构建系统
- 内核链接到 `0x80200000`
- OpenSBI 进入 `_start`
- early stack 建立
- `.bss` 清零
- 跳入 `kmain`
- SBI 控制台输出
- panic 输出 CSR 诊断信息

关键文件：
- `Makefile`
- `kernel.ld`
- `kern/arch/boot.S`
- `kern/arch/sbi.c`
- `kern/device/console.c`
- `kern/printk.c`
- `kern/panic.c`

当前状态：**已闭环、可运行、已验证**

---

### 2. Sv39 基础分页闭环
已实现：
- 物理内存硬编码探测（QEMU virt：`0x80000000` 起，`2 GiB`）
- `pages[]` 元数据数组
- free-list 物理页分配器
- `page_alloc / page_free / page_decref`
- Sv39 基础页表 walk
- `page_insert / page_lookup / page_remove / translate`
- 内核根页表
- 1GiB identity mapping
- `satp` 开启分页
- `sfence.vma`
- 基础自检 `vm_self_test()`

关键文件：
- `include/arch/vm.h`
- `include/pmap.h`
- `kern/pmap.c`

当前状态：**已闭环、已验证 satp 打开后内核继续运行**

---

### 3. Trap + Timer 中断闭环
已实现：
- trapframe 定义
- trap entry 汇编
- 通用寄存器保存/恢复
- `sstatus/sepc/stval/scause` 保存/恢复
- `stvec` 安装
- SBI timer 编程
- `sie.STIE` + `sstatus.SIE`
- timer interrupt 进入 S 态
- 定时器重复触发

关键文件：
- `include/arch/trap.h`
- `include/arch/csr.h`
- `kern/arch/entry.S`
- `kern/arch/trap.c`
- `kern/arch/sbi.c`

当前状态：**已闭环、已验证可重复处理中断**

---

### 4. Env / 调度器骨架闭环
已实现：
- `envs[NENV]`
- `env_free_list`
- `env_sched_list`
- `curenv`
- Env ID 分配
- runnable / not runnable 状态流转
- 轮转调度器
- 定时器中断驱动调度
- 调度打印与 run 次数统计

关键文件：
- `include/env.h`
- `include/sched.h`
- `kern/env.c`
- `kern/sched.c`
- `kern/arch/trap.c`

当前状态：**骨架闭环已完成，策略层已验证**

---

### 5. 最小 U 态 + 最小 syscall 闭环
已实现：
- 为 Env 创建独立页表根
- 为 demo 用户程序映射用户代码页
- 为 demo 用户程序映射用户栈页
- 初始化用户 trapframe
- `env_pop_tf()` 切换 `satp` 并 `sret` 进入 U 态
- U 态 `ecall` 回到内核
- 最小 syscall 分发：
  - `SYS_putchar`
  - `SYS_getenvid`
  - `SYS_yield`
- 两个 demo 用户 env（`user-a` / `user-b`）轮流运行
- 用户态输出字符 `A/B`
- `SYS_yield` 触发切换
- timer interrupt 在用户运行期间仍然可以抢占并触发调度

关键文件：
- `kern/arch/context.S`
- `kern/env.c`
- `kern/syscall.c`
- `user/demo.S`
- `kern/arch/trap.c`
- `kern/sched.c`

当前状态：**最小闭环已完成并验证通过**

说明：
- 当前最小 syscall/U 态闭环已经证明：
  - 能进入 U 态
  - 能从 U 态陷入 S 态
  - 能从 syscall 返回并继续调度
  - 能在多个用户 env 间轮转

---

## 二、当前属于“已做骨架，但还不算最终完成”的模块

### 1. 调度器
现状：
- 已经可轮转
- 已接 timer interrupt
- 已驱动多个用户 env

还缺：
- 真正与“进程状态机”完整联动
- idle 行为更严谨的定义
- 更细的优先级/时间片策略
- 后续与阻塞、IPC、销毁、fork 语义配合

---

### 2. Trap 异常处理
现状：
- timer interrupt 已处理
- `ecall from U` 已处理

还缺：
- page fault 处理
- 非法指令、访问异常的用户级错误处理策略
- 区分“杀死当前用户 env”与“内核 panic”的精细策略
- CoW fault 处理

---

### 3. Env 抽象
现状：
- 已有最小字段与最小页表支持

还缺：
- `envid2env()`
- `env_destroy()`
- 完整 parent/child 权限语义
- 更完整的生命周期管理
- 后续 fork / IPC 字段

---

### 4. 用户态 demo
现状：
- 目前是内嵌汇编 demo，作用是验证最小 U 态闭环

还缺：
- 规范化 user build 流程
- 更像 MOS 用户程序的运行方式
- ELF 加载进入统一路径

---

## 三、尚未实现的核心任务

### 1. ELF 加载与真正用户程序创建
目标：
- 按文档要求支持 ELF 用户程序
- 从二进制映像加载多个段
- 建立用户栈
- 设置入口地址

当前状态：**未实现**

---

### 2. 系统调用完整集
文档要求但仍未完成：
- `SYS_print_cons`
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

当前状态：**仅完成 `SYS_putchar / SYS_getenvid / SYS_yield`**

---

### 3. 内核态 CoW fork
目标：
- 内核在 `exofork` / fork 路径中复制地址空间
- 使用 `PTE_COW`
- 写 fault 时复制物理页

当前状态：**未实现**

---

### 4. IPC
目标：
- `SYS_ipc_recv`
- `SYS_ipc_try_send`
- 页传递
- 值传递
- 阻塞/唤醒语义

当前状态：**未实现**

---

### 5. 设备 MMIO / virtio 支持
目标：
- `SYS_read_dev`
- `SYS_write_dev`
- virtio MMIO allowlist
- 后续文件系统接入

当前状态：**未实现**

---

### 6. 文件系统支持
目标：
- 文档要求的用户态 FS / virtio 路径
- rootfs 挂载与访问

当前状态：**未实现**

---

### 7. 更完整的用户/内核地址空间布局
目标：
- 形成稳定的 MOS 风格用户地址布局
- 后续支持 `UTOP/USTACK/UTEXT/共享页/IPC/CoW`

当前状态：**仅有最小 demo 级布局**

---

## 四、建议的后续推进顺序

### 第一优先级
1. 完善 trap 异常分发策略
2. 完成 `envid2env / env_destroy / env_set_status` 等基础进程接口
3. 扩展 syscall 到内存管理相关调用：
   - `SYS_mem_alloc`
   - `SYS_mem_map`
   - `SYS_mem_unmap`

### 第二优先级
4. 实现 ELF 加载
5. 将 demo 用户程序替换为真正用户程序创建路径
6. 支持更接近 MOS 的用户库与运行方式

### 第三优先级
7. 实现内核态 CoW fork
8. 实现 IPC
9. 实现设备读写 syscall
10. 接通 virtio / FS

---

## 五、当前阶段的结论

### 已经完成的最关键闭环
当前项目已经不是“只能打印的内核样机”，而是已经具备了下面这条完整链路：

**启动 → 分页 → Trap → Timer → 调度 → 进入 U 态 → `ecall` → syscall → 返回/切换**

这是整个后续 RISC-V MOS 移植最关键的执行主干。

### 当前最重要的未完成点
后续工作的重点已经从“把底层跑起来”转成：

- 扩展 syscall 集
- 完善 Env 进程模型
- 接通真正用户程序装载
- 实现 CoW / IPC / 设备访问 / 文件系统

---

## 六、建议的维护方式
后续每完成一个阶段，建议在本文件中同步更新三项：

1. **已实现并验证通过**
2. **已做骨架但仍需完善**
3. **尚未实现**

这样可以持续把这个文件当作项目状态面板使用。