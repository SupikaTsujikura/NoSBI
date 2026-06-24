ifndef CROSS_COMPILE
CROSS_COMPILE := riscv64-linux-gnu-
endif

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
QEMU    ?= qemu-system-riscv64

TARGET_DIR ?= target
BUILD_DIR ?= build
KERNEL_ELF := $(TARGET_DIR)/mos-riscv.elf
KERNEL_BIN := $(TARGET_DIR)/mos-riscv.bin
USER_PROGS := fsserv init demo reader argvtest writeback course_tests
USER_ELFS := $(patsubst %,$(BUILD_DIR)/user/%.elf,$(USER_PROGS))
USER_EMBED_OBJS := $(patsubst %,$(BUILD_DIR)/user/%_elf.o,$(USER_PROGS))
USER_SRCS := user/fsserv.c user/init.c user/demo.c user/reader.c user/argvtest.c user/writeback.c user/course_tests.c user/syscall.c user/fork.c user/fd.c user/fsipc.c user/compat.c user/printf.c
USER_ASMS := user/crt.S
USER_OBJS := $(patsubst user/%.c,$(BUILD_DIR)/user/%.o,$(USER_SRCS)) \
	$(patsubst user/%.S,$(BUILD_DIR)/user/%.o,$(USER_ASMS))
USER_LIB_OBJS := \
	$(BUILD_DIR)/user/syscall.o \
	$(BUILD_DIR)/user/fork.o \
	$(BUILD_DIR)/user/fd.o \
	$(BUILD_DIR)/user/fsipc.o \
	$(BUILD_DIR)/user/compat.o \
	$(BUILD_DIR)/user/printf.o \
	$(BUILD_DIR)/user/print.o \
	$(BUILD_DIR)/user/string.o

EXTRA_CFLAGS ?=

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
	-Iinclude \
	$(EXTRA_CFLAGS)

USER_CFLAGS := $(CFLAGS) -march=rv64imafd -mabi=lp64d -Iuser
LDFLAGS := -T kernel.ld -nostdlib

KERN_SRCS := \
	kern/init.c \
	kern/pmap.c \
	kern/fs.c \
	kern/ext4.c \
	kern/env.c \
	kern/sched.c \
	kern/syscall.c \
	kern/arch/sbi.c \
	kern/arch/trap.c \
	kern/device/console.c \
	kern/device/plic.c \
	kern/device/virtio_blk.c \
	kern/printk.c \
	kern/panic.c \
	lib/print.c \
	lib/string.c

KERN_OBJS := $(KERN_SRCS:.c=.o) kern/arch/boot.o kern/arch/entry.o kern/arch/context.o $(USER_EMBED_OBJS)

QEMU_FLAGS := -machine virt -m 2G -nographic -bios default -kernel $(KERNEL_ELF)
HOST_CC ?= cc
HOST_EXT4_TEST := $(BUILD_DIR)/ext4-host-test
HOST_EXT4_IMAGE := $(BUILD_DIR)/ext4-host-test.img
ifneq ($(ROOTFS),)
QEMU_FLAGS += -drive file=$(ROOTFS),format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0
endif

.PHONY: all clean run debug objdump test test-build host-ext4-test
.SECONDARY: $(USER_ELFS) $(USER_OBJS)

all: $(KERNEL_ELF) $(KERNEL_BIN)

$(TARGET_DIR):
	mkdir -p $@

$(BUILD_DIR)/user:
	mkdir -p $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/%.o: user/%.c | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/%.o: user/%.S | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/print.o: lib/print.c | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/string.o: lib/string.c | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/%.elf: $(BUILD_DIR)/user/%.o $(USER_LIB_OBJS) $(BUILD_DIR)/user/crt.o user/user.ld | $(BUILD_DIR)/user
	$(LD) -T user/user.ld -nostdlib -o $@ $(BUILD_DIR)/user/$*.o $(USER_LIB_OBJS) $(BUILD_DIR)/user/crt.o

$(BUILD_DIR)/user/%_elf.o: $(BUILD_DIR)/user/%.elf | $(BUILD_DIR)/user
	$(LD) -r -b binary $< -o $@

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

test-build:
	$(MAKE) clean all TARGET_DIR=target-test EXTRA_CFLAGS=-DMOS_TEST_MODE

test:
	$(MAKE) clean all TARGET_DIR=target-test EXTRA_CFLAGS=-DMOS_TEST_MODE
	$(MAKE) --directory=test ROOT_DIR=$(CURDIR) KERNEL_ELF=$(CURDIR)/target-test/mos-riscv.elf test

$(HOST_EXT4_TEST): tools/ext4_host_test.c kern/ext4.c lib/string.c | $(BUILD_DIR)/user
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin \
		-Wno-pointer-to-int-cast -Wno-builtin-declaration-mismatch \
		-Iinclude $^ -o $@

host-ext4-test: $(HOST_EXT4_TEST)
	cp test-rootfs.img $(HOST_EXT4_IMAGE)
	$(HOST_EXT4_TEST) $(HOST_EXT4_IMAGE) keep
	$(HOST_EXT4_TEST) $(HOST_EXT4_IMAGE) verify

clean:
	rm -rf target target-test $(BUILD_DIR) $(KERN_SRCS:.c=.o) kern/arch/boot.o kern/arch/entry.o kern/arch/context.o
	$(MAKE) --directory=test ROOT_DIR=$(CURDIR) clean
