ROOT_DIR ?= ..
KERNEL_ELF ?= $(ROOT_DIR)/target-test/mos-riscv.elf

.PHONY: all test clean

all:
	@true

test:
	chmod +x $(ROOT_DIR)/test/run_test.sh
	$(ROOT_DIR)/test/run_test.sh $(KERNEL_ELF)

clean:
	@true
