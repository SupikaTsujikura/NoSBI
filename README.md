# MOS RISC-V 移植当前状态

本目录现在包含一个**基于 C 的 RV64 MOS 早期移植骨架**。它已经能够在 QEMU `virt` + OpenSBI 环境下启动，完成 Sv39 基础分页，处理时钟中断，具备第一版 Env/调度器骨架，并且已经打通了**最小 U 态 + 最小 syscall + 自动化测试**闭环。

---

## 一、当前已经实现的内容

目前已经覆盖了 [docs/RISC-V 移植.md](docs/RISC-V%20移植.md) 中前几个关键里程碑的最小可运行版本：

1. **内核启动与字符输出**
2. **Sv39 基础内存管理 bring-up**
3. **Trap 入口与时钟中断处理**
4. **Env 表与 runnable 调度骨架**
5. **最小 U 态执行与最小 syscall 闭环**
6. **自动化测试入口 `make test`**

### 1. 独立 RV64 构建系统
已添加：
- [Makefile](Makefile)

特点：
- 默认使用 `riscv64-linux-gnu-` 工具链
- 支持 `make`、`make run`、`make debug`、`make objdump`、`make test`
- 显式关闭 PIC/PIE，避免早期 trap 向量地址解析出错

### 2. 链接脚本
已添加：
- [kernel.ld](kernel.ld)

功能：
- 将内核链接到 `0x80200000`
- 与 OpenSBI 默认跳转地址对齐
- 正确处理 `.text/.rodata/.data/.bss/.sdata/.sbss`
- 保证 `kernel_end` / `bss_end` 稳定可用于早期内存分配

### 3. 启动入口
已添加：
- [kern/arch/boot.S](kern/arch/boot.S)

功能：
- 设置 early stack
- 清空 `.bss`
- 跳转到 `kmain`

### 4. SBI 支持与控制台输出
已添加/实现：
- [kern/arch/sbi.c](kern/arch/sbi.c)
- [kern/device/console.c](kern/device/console.c)

功能：
- SBI 控制台字符输出
- SBI shutdown
- SBI timer 设置（优先 TIME 扩展，失败则回退 legacy 调用）

### 5. 基础打印与 panic
已添加：
- [lib/print.c](lib/print.c)
- [lib/string.c](lib/string.c)
- [kern/printk.c](kern/printk.c)
- [kern/panic.c](kern/panic.c)

功能：
- `printk`
- 基础字符串/内存函数
- panic 时打印关键 CSR 状态

### 6. Sv39 基础内存管理
已添加/实现：
- [include/pmap.h](include/pmap.h)
- [include/arch/vm.h](include/arch/vm.h)
- [kern/pmap.c](kern/pmap.c)

功能：
- 物理内存硬编码探测（QEMU virt：`0x80000000` 起，`2 GiB`）
- `pages[]` 页元数据数组
- free-list 物理页分配器
- `page_alloc / page_free / page_decref`
- Sv39 三层页表 walk
- `page_insert / page_lookup / page_remove / translate`
- 内核根页表
- 1GiB identity mapping
- `satp` 开启分页
- `sfence.vma`
- `vm_self_test()` 基础自检

### 7. Trap 与时钟中断
已添加/实现：
- [include/arch/trap.h](include/arch/trap.h)
- [include/arch/csr.h](include/arch/csr.h)
- [kern/arch/entry.S](kern/arch/entry.S)
- [kern/arch/trap.c](kern/arch/trap.c)

功能：
- 进入 trap 时保存 32 个通用寄存器
- 保存 `sstatus` / `sepc` / `stval` / `scause`
- 使用 `sscratch` 在用户栈与内核栈之间切换
- 安装 `stvec`
- 打开 `sie.STIE` 和 `sstatus.SIE`
- 定时器中断进入 S 态处理
- 区分 interrupt / exception
- 支持 `ecall from U`

### 8. Env / 调度器骨架
已添加/实现：
- [include/env.h](include/env.h)
- [include/sched.h](include/sched.h)
- [kern/env.c](kern/env.c)
- [kern/sched.c](kern/sched.c)

功能：
- `envs[NENV]`
- `env_free_list`
- `env_sched_list`
- `curenv`
- Env ID 分配
- Env 状态切换（`ENV_FREE / ENV_RUNNABLE / ENV_NOT_RUNNABLE`）
- 轮转调度器
- 时间片计数（使用 `env_pri`）
- timer interrupt 驱动调度

### 9. 最小 U 态与最小 syscall 闭环
已添加/实现：
- [include/syscall.h](include/syscall.h)
- [include/arch/context.h](include/arch/context.h)
- [kern/arch/context.S](kern/arch/context.S)
- [kern/syscall.c](kern/syscall.c)
- [user/demo.S](user/demo.S)

功能：
- 为 Env 创建自己的页表根
- 映射最小用户代码页与用户栈页
- 初始化用户 trapframe
- 通过 `sret` 进入 U 态
- U 态 `ecall` 回到内核
- 当前已支持最小 syscall：
  - `SYS_putchar`
  - `SYS_getenvid`
  - `SYS_yield`
- 两个用户 demo env（`user-a` / `user-b`）能够交替运行并输出字符

### 10. 自动化测试
已添加：
- [test/Makefile](test/Makefile)
- [test/run_test.sh](test/run_test.sh)
- [test/validate_output.c](test/validate_output.c)

功能：
- 执行 `make test`
- 自动启动 QEMU
- 捕获串口日志到 `target/test/qemu.log`
- 用 C 语言编写的校验器验证当前已实现功能是否正常

---

## 二、当前设计说明

### 1. 内存管理设计
当前内存管理采用“先跑起来”的保守方案：

- DRAM 起始地址：`0x80000000`
- DRAM 大小：`2 GiB`
- 不解析设备树
- 内核根页表使用 **1GiB identity mapping** 覆盖 DRAM
- 内核分页打开后仍然在相同虚拟地址继续执行
- 不做高半区内核

这样做的目的：
- 降低 bring-up 复杂度
- 先验证 Sv39 基础链路
- 为后续 Env / U 态 / syscall 铺底

### 2. Trap 设计
当前 trap 路径已经能支持：

- timer interrupt
- `ecall from U`

当前仍不支持或未细化：
- 用户态 page fault 策略
- 内核/用户异常的细粒度恢复逻辑
- CoW fault 处理

### 3. 调度设计
当前调度器是一个最小可运行骨架：

- runnable env 放在 `env_sched_list`
- 采用轮转调度
- timer interrupt 或 `SYS_yield` 会触发切换
- 每个 env 的 trapframe 当前是权威上下文
- `env_pop_tf()` 负责切到目标 env 并进入 U 态

### 4. 用户态 demo 设计
当前不是完整用户程序体系，而是一个最小汇编 demo：

- 两个 env 分别传入不同字符 `A` / `B`
- demo 循环：
  - `SYS_putchar`
  - `SYS_yield`
- 作用是验证：
  - 进入 U 态
  - `ecall` 回到 S 态
  - syscall 返回
  - scheduler 轮转
  - timer 中断与用户态执行共存

---

## 三、当前已经验证通过的内容

我已经实际执行并验证通过：

### 构建
```bash
make -C /home/jyx/ortus/RISC-V clean all
```

### 自动测试
```bash
make -C /home/jyx/ortus/RISC-V test
```

测试通过输出类似：
```text
PASS: validated 8432 bytes of QEMU output (A=27, B=34)
```

### 当前测试覆盖点
`make test` 当前会自动验证：

- 内核启动 banner 出现
- Sv39 自检通过
- `satp` 打开后内核继续存活
- `user-a` / `user-b` 成功创建
- 调度器进入第一个用户 env
- 用户态成功输出 `A`
- 用户态成功输出 `B`
- 至少出现一次 timer interrupt
- `A/B` 输出不是一次性的，而是能重复发生

也就是说，它已经自动覆盖了这条闭环：

**启动 → 分页 → Env → 调度 → U 态 → syscall → timer**

---

## 四、当前已实现功能与未实现功能的边界

### 已闭环的部分
以下部分已经不是“骨架”，而是已经打通可运行闭环：

- 内核启动
- SBI 输出
- Sv39 基础分页
- Trap 入口与返回
- timer interrupt
- Env 表与 runnable 调度
- 最小 U 态
- 最小 syscall 闭环
- 自动化测试入口

### 已做骨架、但还不算最终完成
这些模块虽然已经有代码，但仍不是最终形态：

- 调度器
- Trap 异常分发策略
- Env 生命周期管理
- 用户态 demo 运行方式

### 尚未实现的核心任务
仍未实现：

- 通用 ELF 加载
- 完整 syscall 集
  - `SYS_print_cons`
  - `SYS_env_destroy`
  - `SYS_mem_alloc / SYS_mem_map / SYS_mem_unmap`
  - `SYS_exofork`
  - `SYS_env_set_status`
  - `SYS_panic`
  - IPC syscalls
  - 设备 MMIO syscalls
- 内核态 CoW fork
- IPC
- 文件系统
- virtio / MMIO 支持
- 完整 MOS 用户态运行时

---

## 五、如何使用

### 1. 编译
```bash
make
```

### 2. 直接运行
```bash
make run
```

### 3. 自动测试
```bash
make test
```

说明：
- 该命令会先构建内核，再构建 `test/validate_output.c` 这个宿主机侧校验程序。
- 测试脚本 [test/run_test.sh](test/run_test.sh) 现在直接使用 QEMU 的 `-serial file:...` 把串口输出写入 `target/test/qemu.log`，避免某些环境下 shell 重定向/缓冲导致日志为空。
- QEMU 输出会保存到 `target/test/qemu.log`。
- 校验器会先清洗日志中的 `\r`、`\0` 等控制字符，再检查关键输出是否按顺序出现。

如果测试失败，建议优先检查：
- `target/test/qemu.log` 是否为空
- 本机是否安装了 `qemu-system-riscv64`
- 本机是否存在 `riscv64-linux-gnu-` 工具链
- 日志中是否确实出现了内核 banner、`A/B` 用户输出、timer interrupt

### 4. 只构建测试工具
```bash
make test-build
```

### 5. 调试
```bash
make debug
```

### 6. 生成反汇编
```bash
make objdump
```

---

## 六、下一步建议
如果继续往前推进，建议优先顺序如下：

1. 完善基础进程接口：
   - `envid2env`
   - `env_destroy`
   - `env_set_status` 更完整语义
2. 扩展内存相关 syscall：
   - `SYS_mem_alloc`
   - `SYS_mem_map`
   - `SYS_mem_unmap`
3. 加入真正 ELF 加载路径
4. 用真实用户程序替换当前 demo
5. 再做：
   - CoW fork
   - IPC
   - MMIO / virtio
   - 文件系统

---

## 七、当前结论
目前项目已经不再是“只能打印的内核样机”，而是已经具备下面这条最关键的执行主干：

**启动 → 分页 → Trap → Timer → 调度 → 进入 U 态 → `ecall` → syscall → 返回/切换**

这是后续完整 RISC-V MOS 移植最关键的一条主路径。后面的大部分工作，本质上都会建立在这条主路径之上。