CC ?= cc
NM ?= nm
# Optional emulator prefix for running cross-built binaries, e.g. qemu-aarch64.
RUN ?=

# This reference runtime deliberately uses POSIX process primitives
# (fork/waitpid/mmap). On Windows, build it from WSL rather than native
# PowerShell/cmd. Avoid invoking `uname` where it does not exist.
ifeq ($(OS),Windows_NT)
  ifeq ($(strip $(WSL_INTEROP)),)
    $(error Native Windows build is not supported: MMUKO uses POSIX fork/waitpid/mmap. Run `wsl`, cd to this directory, then run `make`)
  endif
endif

MMUKO_ARCH ?= $(shell uname -m)
ifeq ($(MMUKO_ARCH),arm64)
override MMUKO_ARCH := aarch64
endif

ifeq ($(MMUKO_ARCH),x86_64)
ARCH_DIR := arch/x86_64/sysv
else ifeq ($(MMUKO_ARCH),aarch64)
ARCH_DIR := arch/aarch64/aapcs64
else
$(error Unsupported architecture $(MMUKO_ARCH); supported: x86_64, aarch64/arm64)
endif

BUILD ?= build

# include/ is public; src/context and the arch directory hold the private
# context-binding headers (never included by kernel or module code).
CPPFLAGS := -Iinclude -Isrc/context -I$(ARCH_DIR)
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDFLAGS :=

# Execution plane - shared context infrastructure (freestanding).
CONTEXT_C := src/context/mmuko_context.c $(ARCH_DIR)/mmuko_context_arch.c
CONTEXT_S := $(ARCH_DIR)/mmuko_context_switch.S
# Execution plane - Cluster A (kernel/TUI) and Cluster B (module/GUI).
KERNEL_C := src/kernel/mmuko_boot.c src/kernel/main.c src/kernel/mmuko_kernel_context.c
MODULE_C := src/module/mmuko_registry.c src/module/orion_stub.c src/module/main.c src/module/mmuko_module_context.c
ENTRY_S := $(ARCH_DIR)/mmuko_entry.S
# Data plane.
DATA_C := src/adapter/mmuko_adapter.c src/adapter/mmuko_channel.c src/platform/posix/mmuko_transport.c
# Control plane (scheduler + topology are freestanding; supervisor is POSIX).
CONTROL_C := src/program/mmuko_abi.c src/program/mmuko_topology.c src/program/mmuko_scheduler.c
PLATFORM_C := src/platform/posix/mmuko_stack.c
PROGRAM_C := src/program/mmuko_supervisor.c src/program/mmuko_program.c src/program/main.c

obj = $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(1))) $(patsubst %.S,$(BUILD)/%.o,$(filter %.S,$(1)))

CONTEXT_O := $(call obj,$(CONTEXT_C) $(CONTEXT_S))
KERNEL_O := $(call obj,$(KERNEL_C))
MODULE_O := $(call obj,$(MODULE_C))
ENTRY_O := $(call obj,$(ENTRY_S))
DATA_O := $(call obj,$(DATA_C))
CONTROL_O := $(call obj,$(CONTROL_C))
PLATFORM_O := $(call obj,$(PLATFORM_C))
PROGRAM_O := $(call obj,$(PROGRAM_C))
CORE_O := $(CONTEXT_O) $(KERNEL_O) $(MODULE_O) $(ENTRY_O) $(DATA_O) $(CONTROL_O) $(PLATFORM_O)

TEST_PROBE_O := $(call obj,tests/arch/$(MMUKO_ARCH)/mmuko_test_probe.S)

# Symbols the architecture requires to exist in the final runtime.
REQUIRED_SYMBOLS := main mmuko_program_main mmuko_kernel_main mmuko_module_main \
	mmuko_context_init mmuko_context_switch mmuko_context_yield \
	mmuko_kernel_context_entry mmuko_module_context_entry

.PHONY: all demo test unit-test context-test integration-test fault-test separation-test \
	symbols-test freestanding-check aarch64-test clean help

all: $(BUILD)/mmuko

help:
	@echo "Targets:"
	@echo "  make                     build the MMUKO runtime ($(MMUKO_ARCH))"
	@echo "  make demo                run bootstrap -> supervisor -> context scheduler and print the log"
	@echo "  make test                unit + context + separation + symbol + integration tests"
	@echo "  make context-test        context switching, ABI layout, register preservation, scheduler"
	@echo "  make fault-test          context-level (kernel, module) and process-level (domain) faults"
	@echo "  make separation-test     kernel/module/control-plane include and link separation"
	@echo "  make freestanding-check  context core, scheduler and data plane build -ffreestanding -nostdinc"
	@echo "  make aarch64-test        cross-build for AArch64 and run every test under qemu-aarch64"
	@echo "  make clean               remove build output"

$(BUILD)/mmuko: $(CORE_O) $(PROGRAM_O)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# ---- tests -------------------------------------------------------------------

$(BUILD)/test_adapter: tests/test_adapter.c $(DATA_O)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD)/test_topology: tests/test_topology.c $(BUILD)/src/program/mmuko_topology.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD)/test_entry: tests/test_entry.c $(CORE_O)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD)/test_context: tests/test_context.c $(CONTEXT_O) $(TEST_PROBE_O)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD)/test_scheduler: tests/test_scheduler.c $(CONTEXT_O) $(CONTROL_O) $(DATA_O) $(PLATFORM_O)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

unit-test: $(BUILD)/test_adapter $(BUILD)/test_topology $(BUILD)/test_entry
	$(RUN) $(BUILD)/test_adapter
	$(RUN) $(BUILD)/test_topology
	$(RUN) $(BUILD)/test_entry

context-test: $(BUILD)/test_context $(BUILD)/test_scheduler
	$(RUN) $(BUILD)/test_context
	$(RUN) $(BUILD)/test_scheduler

integration-test: $(BUILD)/mmuko
	@sh tests/run_runtime.sh $(BUILD)/integration.log $(RUN) $(BUILD)/mmuko
	@sh tests/check_runtime_log.sh normal $(BUILD)/integration.log

test: unit-test context-test separation-test symbols-test integration-test

fault-test: $(BUILD)/mmuko
	@MMUKO_FAULT_INJECT=module sh tests/run_runtime.sh $(BUILD)/fault-module.log $(RUN) $(BUILD)/mmuko
	@sh tests/check_runtime_log.sh module-fault $(BUILD)/fault-module.log
	@MMUKO_FAULT_INJECT=kernel sh tests/run_runtime.sh $(BUILD)/fault-kernel.log $(RUN) $(BUILD)/mmuko
	@sh tests/check_runtime_log.sh kernel-fault $(BUILD)/fault-kernel.log
	@MMUKO_FAULT_INJECT=domain sh tests/run_runtime.sh $(BUILD)/fault-domain.log $(RUN) $(BUILD)/mmuko
	@sh tests/check_runtime_log.sh domain-crash $(BUILD)/fault-domain.log
	@echo "fault tolerance: HELD"

# Source-level and link-level separation. Kernel and module may use the
# neutral context/adapter/channel interfaces, never each other, the control
# plane, or the architecture-private context layout.
separation-test: $(KERNEL_O) $(MODULE_O) $(CONTEXT_O) $(DATA_O)
	@! grep -REn '#[[:space:]]*include[[:space:]]*[<"](mmuko/(module|scheduler|program|stack)|mmuko_context_(arch|layout))' src/kernel \
		|| (echo 'separation violated: kernel imports module, control-plane, or arch-private context headers' && exit 1)
	@! grep -REn '#[[:space:]]*include[[:space:]]*[<"](mmuko/(kernel|scheduler|program|stack)|mmuko_context_(arch|layout))' src/module \
		|| (echo 'separation violated: module imports kernel, control-plane, or arch-private context headers' && exit 1)
	@! grep -REn '#[[:space:]]*include[[:space:]]*[<"]mmuko/(kernel|module|scheduler|program)' src/context src/adapter \
		|| (echo 'separation violated: shared infrastructure imports a concern' && exit 1)
	@! $(NM) -u $(KERNEL_O) | grep -E 'mmuko_(module|scheduler|program|stack)_' \
		|| (echo 'separation violated: kernel objects reference module/control-plane symbols' && exit 1)
	@! $(NM) -u $(MODULE_O) | grep -E 'mmuko_(kernel|scheduler|program|stack)_' \
		|| (echo 'separation violated: module objects reference kernel/control-plane symbols' && exit 1)
	@! $(NM) -u $(CONTEXT_O) $(DATA_O) | grep -E 'mmuko_(kernel|module|scheduler|program)_' \
		|| (echo 'separation violated: shared infrastructure references a concern' && exit 1)
	@echo "separation: HELD"

symbols-test: $(BUILD)/mmuko
	@for sym in $(REQUIRED_SYMBOLS); do \
		$(NM) $(BUILD)/mmuko | grep -qE "^[0-9a-fA-F]+ T $${sym}\$$" \
			|| { echo "missing required symbol: $$sym"; exit 1; }; \
	done
	@echo "required symbols: HELD ($(words $(REQUIRED_SYMBOLS)) defined)"

# ---- freestanding core -------------------------------------------------------
# The context core, both ABI bindings, the scheduler, topology, adapter codec,
# in-domain channel and kernel boot phases compile with no libc headers and
# reference no symbols beyond MMUKO's own and the four memory primitives a
# freestanding C compiler may emit (memcpy/memmove/memset/memcmp).

FREESTANDING_DIR := $(BUILD)/freestanding
FREESTANDING_SRC := src/context/mmuko_context.c $(ARCH_DIR)/mmuko_context_arch.c $(CONTEXT_S) \
	src/program/mmuko_scheduler.c src/program/mmuko_topology.c src/program/mmuko_abi.c \
	src/adapter/mmuko_adapter.c src/adapter/mmuko_channel.c src/kernel/mmuko_boot.c
FREESTANDING_O := $(patsubst %.c,$(FREESTANDING_DIR)/%.o,$(filter %.c,$(FREESTANDING_SRC))) \
	$(patsubst %.S,$(FREESTANDING_DIR)/%.o,$(filter %.S,$(FREESTANDING_SRC)))
FREESTANDING_FLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -ffreestanding -fno-builtin -fno-pic \
	-nostdinc -isystem $(shell $(CC) -print-file-name=include)

$(FREESTANDING_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(FREESTANDING_FLAGS) -c $< -o $@

$(FREESTANDING_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(FREESTANDING_FLAGS) -c $< -o $@

freestanding-check: $(FREESTANDING_O)
	@$(NM) -u $(FREESTANDING_O) | awk 'NF >= 2 { print $$2 }' | sort -u > $(FREESTANDING_DIR)/undefined.txt
	@if grep -vE '^(mmuko_[A-Za-z0-9_]+|memcpy|memmove|memset|memcmp)$$' $(FREESTANDING_DIR)/undefined.txt; then \
		echo 'freestanding core references the symbols listed above'; exit 1; fi
	@echo "freestanding core: HELD ($(words $(FREESTANDING_O)) objects, -ffreestanding -nostdinc, no libc symbols)"

# ---- cross architecture ------------------------------------------------------

AARCH64_CC ?= aarch64-linux-gnu-gcc
AARCH64_NM ?= aarch64-linux-gnu-nm
AARCH64_SYSROOT ?= /usr/aarch64-linux-gnu
AARCH64_RUN ?= qemu-aarch64 -L $(AARCH64_SYSROOT)

aarch64-test:
	@command -v $(AARCH64_CC) >/dev/null 2>&1 || { echo "aarch64-test needs $(AARCH64_CC): sudo apt install gcc-aarch64-linux-gnu qemu-user"; exit 1; }
	@command -v $(firstword $(AARCH64_RUN)) >/dev/null 2>&1 || { echo "aarch64-test needs $(firstword $(AARCH64_RUN)): sudo apt install qemu-user"; exit 1; }
	$(MAKE) MMUKO_ARCH=aarch64 CC=$(AARCH64_CC) NM=$(AARCH64_NM) BUILD=$(BUILD)/aarch64 RUN="$(AARCH64_RUN)" \
		test fault-test freestanding-check

# ---- demo / clean ------------------------------------------------------------

# The bootstrap parent retires early by design; run_runtime.sh waits for the
# supervisor to finish before the log is shown.
demo: $(BUILD)/mmuko
	@sh tests/run_runtime.sh $(BUILD)/demo.log $(RUN) $(BUILD)/mmuko
	@cat $(BUILD)/demo.log

clean:
	rm -rf $(BUILD)
