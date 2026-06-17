HOST_CC ?= cc
ROOT_DIR ?= ..
VALIDATOR := $(ROOT_DIR)/test/validate-output
VALIDATOR_SRC := $(ROOT_DIR)/test/validate_output.c
KERNEL_ELF ?= $(ROOT_DIR)/target/mos-riscv.elf

.PHONY: all test clean

all: $(VALIDATOR)

$(VALIDATOR): $(VALIDATOR_SRC)
	$(HOST_CC) -Wall -Wextra -Werror -O2 -o $@ $<

test: $(VALIDATOR)
	chmod +x $(ROOT_DIR)/test/run_test.sh
	$(ROOT_DIR)/test/run_test.sh $(KERNEL_ELF)

clean:
	rm -f $(VALIDATOR)
