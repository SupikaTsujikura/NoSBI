ifndef CROSS_COMPILE
CROSS_COMPILE := riscv64-linux-gnu-
endif

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
QEMU    ?= qemu-system-riscv64

TARGET_DIR := target
KERNEL_ELF := $(TARGET_DIR)/mos-riscv.elf
KERNEL_BIN := $(TARGET_DIR)/mos-riscv.bin

CFLAGS := \
	-march=rv64imafdch \
	-mabi=lp64d \
	-mcmodel=medany \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-fno-omit-frame-pointer \
	-fno-pic \
	-fno-pie \
	-Wall -Wextra -Werror -ggdb -O2 \
	-Iinclude

LDFLAGS := -T kernel.ld -nostdlib

KERN_SRCS := \
	kern/init.c \
	kern/pmap.c \
	kern/env.c \
	kern/sched.c \
	kern/syscall.c \
	kern/arch/sbi.c \
	kern/arch/trap.c \
	kern/device/console.c \
	kern/printk.c \
	kern/panic.c \
	lib/print.c \
	lib/string.c

KERN_OBJS := $(KERN_SRCS:.c=.o) kern/arch/boot.o kern/arch/entry.o kern/arch/context.o user/demo.o

QEMU_FLAGS := -machine virt -m 2G -nographic -bios default -kernel $(KERNEL_ELF)

.PHONY: all clean run debug objdump

all: $(KERNEL_ELF) $(KERNEL_BIN)

$(TARGET_DIR):
	mkdir -p $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERN_OBJS) | $(TARGET_DIR)
	$(LD) $(LDFLAGS) -o $@ $(KERN_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

run: $(KERNEL_ELF)
	$(QEMU) $(QEMU_FLAGS)

debug: $(KERNEL_ELF)
	$(QEMU) $(QEMU_FLAGS) -s -S

objdump: $(KERNEL_ELF)
	$(OBJDUMP) -aldS $(KERNEL_ELF) > $(KERNEL_ELF).objdump

clean:
	rm -rf $(TARGET_DIR) $(KERN_OBJS)
