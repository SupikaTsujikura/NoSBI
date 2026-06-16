# MOS on RISC-V: current porting status

This directory now contains the **initial C-based RV64 kernel skeleton** for porting MOS onto QEMU `virt` with OpenSBI.

## What has been implemented

This first implementation slice intentionally focuses on the very first milestone from [docs/RISC-V 移植.md](docs/RISC-V%20移植.md): **booting the kernel and printing output**.

Implemented pieces:

- **Standalone RV64 build system**
  - Added [Makefile](Makefile) for building, running, debugging, and objdump generation.
  - Default toolchain prefix is `riscv64-linux-gnu-` because that is what is available in the current environment.
- **Linker script**
  - Added [kernel.ld](kernel.ld) that links the kernel to `0x80200000`, matching the OpenSBI handoff address documented in [docs/RISC-V 移植.md](docs/RISC-V%20移植.md).
- **Boot entry**
  - Added [kern/arch/boot.S](kern/arch/boot.S).
  - Sets up an early boot stack.
  - Clears `.bss`.
  - Transfers control to `kmain`.
- **Minimal SBI support**
  - Added [kern/arch/sbi.c](kern/arch/sbi.c).
  - Supports legacy SBI console putchar and SBI system shutdown.
- **Console backend**
  - Added [kern/device/console.c](kern/device/console.c).
  - Hooks kernel character output to SBI.
- **Basic kernel formatting/printing runtime**
  - Added [lib/print.c](lib/print.c), [lib/string.c](lib/string.c), [kern/printk.c](kern/printk.c), and related headers.
- **Early panic path**
  - Added [kern/panic.c](kern/panic.c).
  - Dumps key S-mode CSR state (`sstatus`, `sepc`, `scause`, `stval`, `stvec`) before halting.
- **Header skeleton for later stages**
  - Added initial headers under [include/](include/) and [include/arch/](include/arch/), including type definitions, queue macros, syscall numbers, trapframe skeletons, and CSR helpers.
- **Early kernel main**
  - Added [kern/init.c](kern/init.c).
  - Prints boot diagnostics and then deliberately panics so the bring-up state is visible and deterministic.

## What was intentionally *not* implemented yet

The following required porting stages are **not done yet** in this slice:

- Sv39 page tables and physical memory allocator
- trap entry/return path
- timer interrupts
- process/env management
- scheduler
- syscalls beyond placeholder numbering/header definitions
- user-mode entry
- kernel-side CoW fork
- IPC
- MMIO device access for virtio
- file system integration

## Design changes vs original MOS

This port already starts correcting a few weak points from the MIPS tree instead of cloning them blindly:

1. **Architecture split is cleaner**
   - RISC-V-specific code now begins under [include/arch/](include/arch/) and [kern/arch/](kern/arch/), instead of mixing machine details into one global `mmu.h`-style header.

2. **Toolchain and machine assumptions are explicit**
   - The build is targeted directly at `qemu-system-riscv64 -machine virt` with OpenSBI, rather than inheriting MIPS Malta assumptions.

3. **String/memory helpers were adapted for RV64**
   - The copied MOS string routines were adjusted to use 64-bit word copies/fills where appropriate instead of preserving the 32-bit MIPS-oriented implementation unchanged.

4. **Panic output is S-mode aware**
   - Panic diagnostics now report RISC-V CSR state rather than CP0/TLB-era MIPS registers.

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

## Verified behavior

The current kernel was built and booted successfully in QEMU in this environment.

Observed boot result:

- OpenSBI loads and jumps to the kernel at `0x80200000`
- the kernel prints:
  - `MOS/RISC-V bootstrap`
  - current `sstatus`
  - current `stvec`
- the kernel then enters a deliberate panic showing CSR state

This confirms that:

- the linker script address is correct
- `_start` is reached successfully
- the boot stack works
- `.bss` clearing does not crash
- C code runs correctly after assembly entry
- SBI console output is functional
- the panic path is functional

## Current file map

Most relevant files added in this slice:

- [Makefile](Makefile)
- [kernel.ld](kernel.ld)
- [kern/arch/boot.S](kern/arch/boot.S)
- [kern/arch/sbi.c](kern/arch/sbi.c)
- [kern/device/console.c](kern/device/console.c)
- [kern/init.c](kern/init.c)
- [kern/printk.c](kern/printk.c)
- [kern/panic.c](kern/panic.c)
- [lib/print.c](lib/print.c)
- [lib/string.c](lib/string.c)
- [include/](include/)
- [include/arch/](include/arch/)

## Next recommended step

The next implementation step should be:

1. add the RV64/Sv39 memory layout and page-table code,
2. build a physical page allocator,
3. create a kernel root page table,
4. enable paging with `satp` + `sfence.vma`,
5. then move on to trap entry and timer interrupts.

That is the smallest safe path toward the next required milestones in [docs/RISC-V 移植.md](docs/RISC-V%20移植.md).
