# MOS on RISC-V: current porting status

This directory now contains an **early C-based RV64 MOS port skeleton** that already boots on QEMU `virt` via OpenSBI and has completed the first memory-management bring-up step.

## What has been implemented

This repository now covers two early milestones from [docs/RISC-V 移植.md](docs/RISC-V%20移植.md):

1. **kernel boot + character output**
2. **initial Sv39 memory-management bring-up**

Implemented pieces:

- **Standalone RV64 build system**
  - Added [Makefile](Makefile) for build, run, debug, and objdump generation.
  - Uses `riscv64-linux-gnu-` by default because that toolchain exists in the current environment.
- **Linker script**
  - Added [kernel.ld](kernel.ld), linking the kernel at `0x80200000` to match the OpenSBI handoff documented in [docs/RISC-V 移植.md](docs/RISC-V%20移植.md).
- **Boot entry**
  - Added [kern/arch/boot.S](kern/arch/boot.S).
  - Sets up an early stack.
  - Clears `.bss`.
  - Transfers control to `kmain`.
- **Minimal SBI support**
  - Added [kern/arch/sbi.c](kern/arch/sbi.c).
  - Supports legacy console putchar and SBI shutdown.
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
    - [include/arch/riscv.h](include/arch/riscv.h)
    - [include/arch/csr.h](include/arch/csr.h)
    - [include/arch/vm.h](include/arch/vm.h)
    - [include/arch/trap.h](include/arch/trap.h)
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
- **Early kernel main**
  - Updated [kern/init.c](kern/init.c).
  - Now initializes memory management, runs a self-test, enables paging, prints diagnostics, and then deliberately panics.

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

This is intentionally conservative. It is enough to:

- allocate memory
- walk page tables
- install one test mapping
- enable Sv39 cleanly
- keep the kernel alive after the `satp` switch

## What has been verified

The current kernel was rebuilt and booted successfully in QEMU after the Sv39 changes.

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
- `sstatus`
- `stvec`
- detected page count (`npage`)
- maximum physical address (`maxpa`)
- `pages[]` address
- kernel page-table root address
- `vm self-test passed`
- `satp enabled, kernel still alive`

Then it enters an intentional panic.

This confirms that:

- early physical-memory bookkeeping works
- boot allocator works
- page metadata allocation works
- free-list allocator works at least for basic allocation
- Sv39 page-table walk and leaf insertion work for the current self-test
- `satp` switching does not immediately kill the kernel
- `sfence.vma` path is wired in

## Design changes vs original MOS

This port is not a blind copy of the MIPS tree. Several weak spots are already being corrected.

1. **Architecture split is cleaner**
   - RISC-V-specific code lives under [include/arch/](include/arch/) and [kern/arch/](kern/arch/), instead of mixing machine assumptions into one giant MIPS-centric header.

2. **Memory model no longer depends on MIPS KSEG rules**
   - This port does not reuse `KSEG0/KSEG1/ULIM/PADDR/KADDR` conventions from the MIPS tree.
   - Physical-to-kernel conversions are explicit and based on the QEMU `virt` DRAM range.

3. **String/memory helpers were adapted for RV64**
   - The copied MOS string routines were adjusted to use 64-bit words where appropriate instead of keeping 32-bit assumptions unchanged.

4. **Paging bring-up is simpler and less fragile than the original MIPS TLB path**
   - The current step avoids MIPS software-managed TLB assumptions entirely.
   - It uses standard Sv39 page tables and `satp`/`sfence.vma`, which is the right foundation for later syscall/process work.

5. **Future CoW support was planned into the bit layout**
   - `PTE_COW` and `PTE_LIBRARY` software bits are defined now so the later fork/IPC work can land without redoing the PTE abstraction.

## What is still intentionally missing

The following required stages are **still not implemented yet**:

- trap entry / restore path
- timer interrupt support
- process/env management
- scheduler
- syscalls beyond numbering/header skeletons
- user-mode entry
- kernel-side CoW fork
- IPC
- MMIO access controls for virtio
- filesystem integration
- a final user/kernel virtual memory layout compatible with full MOS userland

Also note:

- kernel permissions are still coarse because the current root mapping is a 1 GiB RWX leaf for bring-up simplicity
- device tree parsing is still omitted
- there is no high-half kernel yet
- no trap vector has been installed yet

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
- [kern/arch/sbi.c](kern/arch/sbi.c)
- [kern/device/console.c](kern/device/console.c)
- [kern/init.c](kern/init.c)
- [kern/pmap.c](kern/pmap.c)
- [kern/printk.c](kern/printk.c)
- [kern/panic.c](kern/panic.c)
- [lib/print.c](lib/print.c)
- [lib/string.c](lib/string.c)
- [include/pmap.h](include/pmap.h)
- [include/arch/vm.h](include/arch/vm.h)
- [include/arch/csr.h](include/arch/csr.h)

## Next recommended step

The next implementation step should be:

1. install a real trap entry path,
2. add timer interrupt support,
3. distinguish user vs kernel trap origin,
4. then start building `Env` / scheduler / syscall entry on top of the now-working Sv39 base.

That is the smallest safe path toward the next mandatory milestone in [docs/RISC-V 移植.md](docs/RISC-V%20移植.md).
