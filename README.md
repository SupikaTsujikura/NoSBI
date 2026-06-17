# MOS on RISC-V: current porting status

This directory now contains an **early C-based RV64 MOS port skeleton** that boots on QEMU `virt`, enables Sv39 paging, handles timer interrupts, and now has a first Env/scheduler skeleton wired into those interrupts.

## What has been implemented

This repository now covers four early milestones from [docs/RISC-V 移植.md](docs/RISC-V%20移植.md):

1. **kernel boot + character output**
2. **initial Sv39 memory-management bring-up**
3. **initial trap entry + timer interrupt handling**
4. **initial Env table + runnable scheduling skeleton**

Implemented pieces:

- **Standalone RV64 build system**
  - Added [Makefile](Makefile) for build, run, debug, and objdump generation.
  - Uses `riscv64-linux-gnu-` by default because that toolchain exists in the current environment.
  - Build flags explicitly disable PIC/PIE so early trap vectors resolve to direct kernel addresses.
- **Linker script**
  - Added [kernel.ld](kernel.ld), linking the kernel at `0x80200000` to match the OpenSBI handoff documented in [docs/RISC-V 移植.md](docs/RISC-V%20移植.md).
  - The linker script accounts for `.sdata`, `.sbss`, and aligned BSS end handling so early memory sizing is stable.
- **Boot entry**
  - Added [kern/arch/boot.S](kern/arch/boot.S).
  - Sets up an early stack.
  - Clears `.bss`.
  - Transfers control to `kmain`.
- **Minimal SBI support**
  - Added [kern/arch/sbi.c](kern/arch/sbi.c).
  - Supports legacy console putchar and SBI shutdown.
  - Supports timer programming through the SBI TIME extension, with a fallback to the legacy set-timer call.
- **Console backend**
  - Added [kern/device/console.c](kern/device/console.c).
  - Hooks kernel character output to SBI.
- **Basic kernel formatting/printing runtime**
  - Added [lib/print.c](lib/print.c), [lib/string.c](lib/string.c), [kern/printk.c](kern/printk.c), and related headers.
- **Early panic path**
  - Added [kern/panic.c](kern/panic.c).
  - Dumps S-mode CSR state before halting.
- **RV64/Sv39 header skeleton**
  - Added or expanded:
    - [include/types.h](include/types.h)
    - [include/error.h](include/error.h)
    - [include/queue.h](include/queue.h)
    - [include/pmap.h](include/pmap.h)
    - [include/env.h](include/env.h)
    - [include/sched.h](include/sched.h)
    - [include/arch/riscv.h](include/arch/riscv.h)
    - [include/arch/csr.h](include/arch/csr.h)
    - [include/arch/vm.h](include/arch/vm.h)
    - [include/arch/trap.h](include/arch/trap.h)
    - [include/arch/sbi.h](include/arch/sbi.h)
- **Initial physical memory and paging implementation**
  - Added [kern/pmap.c](kern/pmap.c).
  - Implements:
    - physical memory sizing for QEMU `virt`
    - boot-time bump allocation
    - `struct Page` metadata array
    - free-list page allocator
    - Sv39 page-table walk helper
    - page insertion / lookup / removal helpers
    - kernel root page table setup
    - `satp` write + `sfence.vma`
- **Initial trap and timer support**
  - Added [kern/arch/entry.S](kern/arch/entry.S).
  - Added [kern/arch/trap.c](kern/arch/trap.c).
  - Implements:
    - full general-register trap save/restore
    - CSR save/restore for `sstatus`, `sepc`, `stval`, `scause`
    - `stvec` installation
    - `sie.STIE` enable
    - timer interrupt scheduling through SBI
    - first C-side interrupt/exception dispatch split
- **Initial Env / scheduler skeleton**
  - Added [kern/env.c](kern/env.c).
  - Added [kern/sched.c](kern/sched.c).
  - Implements:
    - global `envs[NENV]`
    - free env list
    - runnable env queue
    - env id generation
    - simple env allocation
    - status transitions into and out of the runnable queue
    - a first round-robin scheduler with MOS-style slice counting
    - timer interrupt hook into scheduling
- **Early kernel main**
  - Updated [kern/init.c](kern/init.c).
  - Now initializes paging, creates a few demo runnable envs, installs the trap handler, waits for timer-driven scheduling activity, confirms rotations, and then deliberately panics.

## Current memory-management design

This step intentionally uses the simplest safe bring-up strategy rather than the final full MOS memory layout.

### Physical memory assumptions

- DRAM base is hard-coded as `0x80000000`
- DRAM size is currently hard-coded as `2 GiB`
- this matches the current QEMU invocation in [Makefile](Makefile)
- device tree parsing is intentionally deferred

### Page metadata

- physical memory is split into 4 KiB pages
- `pages[]` is allocated early via `boot_alloc`
- free pages are managed through a MOS-style intrusive free list
- `struct Page.pp_ref` is used as the basic reference count

### Page tables

- Sv39 constants and PTE encoding live in [include/arch/vm.h](include/arch/vm.h)
- a basic 3-level walker is implemented in [kern/pmap.c](kern/pmap.c)
- software-reserved bits for future `PTE_COW` and `PTE_LIBRARY` were defined up front

### Kernel mapping strategy

For this early stage, the kernel uses a deliberately simple mapping:

- a **1 GiB root-level identity mapping** covering DRAM from `0x80000000`
- the kernel continues executing at the same virtual addresses after paging is enabled
- this avoids a high-half transition during bring-up

## Current trap and timer design

This is the first working trap slice, not the final process-aware trap system.

### Trap entry

[kern/arch/entry.S](kern/arch/entry.S) currently:

- allocates a `Trapframe` on the kernel stack
- saves all 32 general-purpose registers
- records:
  - `sstatus`
  - `sepc`
  - `stval`
  - `scause`
- calls `trap_entry_c(tf)`
- restores register state and returns with `sret`

This is enough for early kernel-only interrupt bring-up.

### Trap dispatch

[kern/arch/trap.c](kern/arch/trap.c) currently distinguishes:

- **interrupts** vs **exceptions** using the top bit of `scause`
- supervisor timer interrupts (`scause = interrupt | 5`)
- placeholder user `ecall` handling

Right now:

- timer interrupts are serviced and re-armed
- timer interrupts also call the first scheduler path
- unknown interrupts panic
- unknown exceptions print the trapframe and panic
- user syscalls are not implemented yet

### Timer handling

The timer path uses SBI rather than direct machine timer access:

- `trap_init()` installs `stvec`
- enables `sie.STIE`
- arms the first timer event with `sbi_set_timer()`
- enables `sstatus.SIE`
- each timer interrupt increments `timer_ticks`, re-arms the timer, and invokes scheduling

This matches the documented RISC-V porting expectation much better than trying to port the MIPS CP0 clock path.

## Current Env and scheduling design

This is the first scheduling skeleton, not a full process switch implementation.

### Env layer

[kern/env.c](kern/env.c) currently provides:

- `envs[NENV]`
- `env_free_list`
- `env_sched_list`
- `curenv`
- `env_init()`
- `env_alloc()`
- `env_set_status()`
- `env_create_kernel_demo()`

At this stage, envs are **kernel-side scheduling objects only**:

- there is no user address space per env yet
- there is no per-env `satp` switch yet
- there is no `env_run()` that restores a user trapframe
- there is no destruction/free path yet

### Scheduler

[kern/sched.c](kern/sched.c) currently provides:

- `sched_init()`
- `schedule(int yield)`
- MOS-style slice counting using `env_pri`
- runnable queue rotation on timer/yield paths

Current semantics:

- if the current env is runnable and a reschedule happens, it moves to the tail
- the next runnable env is taken from the head
- if no env is runnable, `curenv` becomes `NULL`
- instead of entering user mode, the scheduler currently records the selected env and prints the transition

This is intentional: it validates the policy layer before adding real context switch mechanics.

## What has been verified

The current kernel was rebuilt and booted successfully in QEMU after the Env/scheduler changes.

### Verified build

```bash
make -C /home/jyx/ortus/RISC-V clean all
```

### Verified run

```bash
make -C /home/jyx/ortus/RISC-V run
```

### Observed runtime behavior

The kernel now prints:

- boot banner
- paging bring-up diagnostics
- three created demo envs
- installed trap-vector address
- timer interrupts
- scheduler rotations across runnable envs
- final current-env summary
- final panic after the scheduling test completes

The scheduling output was explicitly observed as:

- `env created: id=... name=idle`
- `env created: id=... name=worker-a`
- `env created: id=... name=worker-b`
- `timer interrupt #1`
- `schedule -> env=... name=idle`
- `timer interrupt #2`
- `schedule -> env=... name=worker-a`
- `timer interrupt #3`
- `schedule -> env=... name=worker-b`
- `timer interrupt #4`
- `schedule -> env=... name=idle`
- `timer interrupt #5`
- `schedule -> env=... name=worker-a`

This confirms that:

- early physical-memory bookkeeping works
- paging still survives after the added env/scheduler code
- trap entry/return still survives repeated timer interrupts
- runnable-list rotation works
- priority-based time-slice accounting works at the skeleton level
- timer interrupts now drive scheduler policy rather than only proving liveness

## Design changes vs original MOS

This port is not a blind copy of the MIPS tree. Several weak spots are already being corrected.

1. **Architecture split is cleaner**
   - RISC-V-specific code lives under [include/arch/](include/arch/) and [kern/arch/](kern/arch/), instead of mixing machine assumptions into one giant MIPS-centric header.

2. **Memory model no longer depends on MIPS KSEG rules**
   - This port does not reuse `KSEG0/KSEG1/ULIM/PADDR/KADDR` conventions from the MIPS tree.
   - Physical-to-kernel conversions are explicit and based on the QEMU `virt` DRAM range.

3. **Trap handling no longer depends on MIPS software-managed TLB structure**
   - The current trap path is built around `stvec`, `scause`, and `sret`, rather than MIPS exception vectors and CP0 cause decoding.

4. **Timer handling now follows SBI expectations**
   - The MIPS Count/Compare clock path was not ported.
   - This kernel uses SBI timer calls, which is the correct portability layer for S-mode on OpenSBI.

5. **Scheduler policy is being separated from context-switch mechanism**
   - The current scheduler already rotates envs and tracks runs before real user-mode switching is added.
   - This reduces bring-up risk compared with trying to land scheduling policy and full context switching at the same time.

6. **String/memory helpers were adapted for RV64**
   - The copied MOS string routines were adjusted to use 64-bit words where appropriate instead of keeping 32-bit assumptions unchanged.

7. **Future CoW support was planned into the bit layout**
   - `PTE_COW` and `PTE_LIBRARY` software bits are defined now so the later fork/IPC work can land without redoing the PTE abstraction.

## What is still intentionally missing

The following required stages are **still not implemented yet**:

- real process context switch / return-to-user path
- user address spaces per env
- syscall dispatch and return semantics
- user-mode entry
- ELF loading
- kernel-side CoW fork
- IPC
- MMIO access controls for virtio
- filesystem integration
- final user/kernel virtual memory layout compatible with full MOS userland

Also note:

- kernel permissions are still coarse because the current root mapping is a 1 GiB identity leaf for bring-up simplicity
- device tree parsing is still omitted
- there is no high-half kernel yet
- exceptions still mostly panic rather than applying MOS process-level policy
- the scheduler currently proves selection/rotation logic, but not yet full architectural context switching

## Build and run

### Build

```bash
make
```

### Run

```bash
make run
```

### Debug under GDB stub

```bash
make debug
```

### Produce annotated disassembly

```bash
make objdump
```

## Current file map

Most relevant files in the current slice:

- [Makefile](Makefile)
- [kernel.ld](kernel.ld)
- [kern/arch/boot.S](kern/arch/boot.S)
- [kern/arch/entry.S](kern/arch/entry.S)
- [kern/arch/sbi.c](kern/arch/sbi.c)
- [kern/arch/trap.c](kern/arch/trap.c)
- [kern/device/console.c](kern/device/console.c)
- [kern/init.c](kern/init.c)
- [kern/pmap.c](kern/pmap.c)
- [kern/env.c](kern/env.c)
- [kern/sched.c](kern/sched.c)
- [kern/printk.c](kern/printk.c)
- [kern/panic.c](kern/panic.c)
- [lib/print.c](lib/print.c)
- [lib/string.c](lib/string.c)
- [include/pmap.h](include/pmap.h)
- [include/env.h](include/env.h)
- [include/sched.h](include/sched.h)
- [include/arch/vm.h](include/arch/vm.h)
- [include/arch/csr.h](include/arch/csr.h)
- [include/arch/trap.h](include/arch/trap.h)
- [include/arch/sbi.h](include/arch/sbi.h)

## Next recommended step

The next implementation step should be:

1. add a real syscall dispatch path for `ecall`,
2. define the first user-mode trapframe conventions,
3. create a minimal user env and return into U-mode,
4. then connect timer interrupts to a real `env_run()` / preemption path.

That is the smallest safe path toward the next mandatory milestone in [docs/RISC-V 移植.md](docs/RISC-V%20移植.md).
