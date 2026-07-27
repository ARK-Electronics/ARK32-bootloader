# host build of the bootloader input signal tests
#
# bootloader/main.c is compiled for the host against the simulated MCU
# in sitl/, so the tests exercise the real boot decision code rather
# than a copy of it.

SITL_DIR := sitl
SITL_OBJ := $(OBJ)/sitl
SITL_BIN := $(SITL_OBJ)/test_input_signal

SITL_CC ?= cc

# the target the simulation models: F051 on PB4, which is the ARK 4IN1
SITL_CFLAGS := -I$(SITL_DIR) -I$(SITL_DIR)/Inc -I$(MAIN_INC_DIR)
SITL_CFLAGS += -DBOOTLOADER -DUSE_PB4 -DDRONECAN_SUPPORT=0 -DAM32_MCU=\"SITL\"
SITL_CFLAGS += -O1 -g -std=gnu11
SITL_CFLAGS += -Wall -Wextra -Wundef -Werror -Wno-unused-parameter

SITL_SRC := $(SITL_DIR)/sitl_hw.c $(SITL_DIR)/signals.c $(SITL_DIR)/test_input_signal.c

# a build of the harness that links the bootloader from a different
# tree, used to show the tests catching the bug they were written for
SITL_BOOTLOADER ?= bootloader/main.c

.PHONY: sitl sitl_test

sitl: $(SITL_BIN)

$(SITL_BIN): $(SITL_SRC) $(SITL_BOOTLOADER) $(SITL_DIR)/sitl.h $(SITL_DIR)/signals.h \
             $(wildcard $(SITL_DIR)/Inc/*.h) $(MAIN_INC_DIR)/version.h
	@echo building bootloader SITL tests
	@mkdir -p $(SITL_OBJ)
	$(QUIET)$(SITL_CC) $(SITL_CFLAGS) -o $@ $(SITL_SRC) $(SITL_BOOTLOADER)

sitl_test: $(SITL_BIN)
	$(QUIET)$(SITL_BIN) $(SITL_TEST_ARGS)
