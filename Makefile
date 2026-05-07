# 顶层 Makefile
TARGET_DIR = target
KERNEL_ELF = kernel.elf
KERNEL_SRC_DIR = kernel

# 通用编译选项（export 后子 Makefile 可以直接使用）
export CC = riscv64-linux-gnu-gcc
export LD = riscv64-linux-gnu-ld
export CFLAGS = -march=rv64imafdch -mcmodel=medany -ffreestanding -nostdlib -nostartfiles -Wall -Wextra -Werror -fno-builtin -fno-stack-protector
export LDFLAGS = -T ../kernel.ld -nostdlib

# 注意：LDFLAGS 中的链接脚本路径是相对于子 Makefile 工作目录的，所以写 ../kernel.ld

all: $(TARGET_DIR)/$(KERNEL_ELF)

$(TARGET_DIR)/$(KERNEL_ELF):
	$(MAKE) -C $(KERNEL_SRC_DIR)
	mkdir -p $(TARGET_DIR)
	cp $(KERNEL_SRC_DIR)/$(KERNEL_ELF) $(TARGET_DIR)/

run: $(TARGET_DIR)/$(KERNEL_ELF)
	qemu-system-riscv64 -machine virt -m 2G -nographic -kernel $<

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) clean
	rm -rf $(TARGET_DIR)

.PHONY: all clean run