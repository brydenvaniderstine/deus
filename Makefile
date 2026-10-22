SHELL        := /bin/bash

BUILD        := build
KERNEL_ELF   := $(BUILD)/kernel.elf
IMAGE        := deus_disk.img
LIMINE_DIR   := limine

CC           := clang
AS           := clang
LD           := ld.lld

# --- freestanding x86_64 kernel flags (CLAUDE.md hard constraints) ---
TARGET_FLAGS := --target=x86_64-elf -m64 -march=x86-64
FREESTANDING := -ffreestanding -fno-stack-protector -fno-stack-check \
                -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
                -mgeneral-regs-only
WARN_FLAGS   := -Wall -Wextra
INCLUDES     := -Ikernel -I$(LIMINE_DIR)

CFLAGS       := $(TARGET_FLAGS) -std=gnu11 $(FREESTANDING) $(WARN_FLAGS) \
                $(INCLUDES) -O2 -g -c
ASFLAGS      := $(TARGET_FLAGS) $(INCLUDES) -g -c

LDFLAGS      := -m elf_x86_64 -T kernel/boot/linker.ld -static -nostdlib -z max-page-size=0x1000

# test_wx.c needs -DWX_TEST=N (see the test-wx target) and is never part
# of the default kernel.elf.
TEST_WX_SRC := kernel/boot/test_wx.c
C_SRCS := $(filter-out $(TEST_WX_SRC),$(shell find kernel -name '*.c'))
S_SRCS := $(shell find kernel -name '*.S')
OBJS   := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
          $(patsubst %.S,$(BUILD)/%.o,$(S_SRCS))

QEMU         := qemu-system-x86_64
# Derived from the actual installed binary, not `brew --prefix qemu` -
# that reports a path unconditionally, even when qemu was installed
# some other way (e.g. a manual ./configure --prefix build) and the
# formula was never `brew link`ed, so the guessed path doesn't exist.
QEMU_PREFIX  := $(shell dirname $(shell dirname $(shell command -v $(QEMU))))
OVMF_CODE    := $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd
OVMF_VARS    := $(QEMU_PREFIX)/share/qemu/edk2-i386-vars.fd

ESP          := $(BUILD)/esp

# Dev boot: QEMU's vvfat driver serves $(ESP) directly as a FAT filesystem,
# no GPT/mtools image needed. `make image` (GPT+FAT32, via mtools/sgdisk)
# is kept as a separate release-only target.
QEMU_FLAGS   := -machine q35 -m 8G -cpu qemu64,+apic,+pdpe1gb \
                -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
                -drive if=pflash,format=raw,file=$(BUILD)/ovmf_vars.fd \
                -drive format=raw,file=fat:rw:$(ESP) \
                -serial file:serial.log -monitor stdio -no-reboot \
                -d int,cpu_reset -D qemu.log

.PHONY: all image run debug clean limine stage-esp test-wx test-lexer test-lexer-asan test-arena test-arena-asan test-ast test-ast-asan test-parser-state test-parser-state-asan test-parser-fixes test-parser-fixes-asan test-parser test-parser-asan test-determinism test-ir test-ir-asan test-ir-unit test-ir-unit-asan test-ir-ndebug test-ir-fix test-ir-fix-asan test-ir-error-codes audit-ir-poison test-e0261-exhaustive coverage fuzz fuzz-lexer fuzz-parser fuzz-ir fuzz-ir-ungated measure-height measure-ir-stack measure-ir-build-stack

all: $(KERNEL_ELF)

# limine/ is gitignored (vendored, not tracked) - fetched on demand at the
# pinned tag so a clean checkout (a fresh clone, or CI) can build without
# a manual setup step first. See tools/fetch-limine.sh for the pinned
# tag/hash.
$(LIMINE_DIR)/limine.h:
	./tools/fetch-limine.sh

$(BUILD)/%.o: %.c | $(LIMINE_DIR)/limine.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/%.o: %.S | $(LIMINE_DIR)/limine.h
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

$(KERNEL_ELF): $(OBJS) kernel/boot/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

limine:
	@test -x $(LIMINE_DIR)/limine || $(MAKE) -C $(LIMINE_DIR)

stage-esp: $(KERNEL_ELF) limine.conf limine
	@mkdir -p $(ESP)/EFI/BOOT $(ESP)/boot
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ESP)/EFI/BOOT/BOOTX64.EFI
	cp limine.conf $(ESP)/limine.conf
	cp $(KERNEL_ELF) $(ESP)/boot/kernel.elf

# Release-only: build a GPT disk with a FAT32 ESP containing BOOTX64.EFI,
# limine.conf, and the kernel ELF. See tools/make_image.sh.
image: $(KERNEL_ELF) limine
	./tools/make_image.sh $(IMAGE) $(KERNEL_ELF) $(LIMINE_DIR)

run: stage-esp
	@mkdir -p $(BUILD)
	cp $(OVMF_VARS) $(BUILD)/ovmf_vars.fd
	$(QEMU) $(QEMU_FLAGS)

debug: stage-esp
	@mkdir -p $(BUILD)
	cp $(OVMF_VARS) $(BUILD)/ovmf_vars.fd
	$(QEMU) $(QEMU_FLAGS) -s -S

# W^X regression tests (never in the default build - see test_wx.c).
# Builds and boots 3 variants, one per deliberate violation, each in its
# own QEMU instance with its own serial/qemu log. No `timeout` binary on
# macOS, so each run is backgrounded, given 5s to boot/fault/halt, then
# killed - that's the full lifetime these need (boot is seconds; the
# expected #PF plus our own cli;hlt happen immediately after).
TEST_WX_DIR := $(BUILD)/test_wx
SHARED_TEST_OBJS := $(filter-out $(BUILD)/kernel/boot/entry.o,$(OBJS))

test-wx: $(SHARED_TEST_OBJS) limine.conf limine
	@mkdir -p $(TEST_WX_DIR)
	@cp $(OVMF_VARS) $(TEST_WX_DIR)/ovmf_vars_src.fd
	@for n in 1 2 3; do \
		echo "=== WX_TEST=$$n ==="; \
		$(CC) $(CFLAGS) -DWX_TEST=$$n -o $(TEST_WX_DIR)/test_wx_$$n.o $(TEST_WX_SRC); \
		$(LD) $(LDFLAGS) -o $(TEST_WX_DIR)/test_wx_$$n.elf $(TEST_WX_DIR)/test_wx_$$n.o $(SHARED_TEST_OBJS); \
		mkdir -p $(TEST_WX_DIR)/esp_$$n/EFI/BOOT $(TEST_WX_DIR)/esp_$$n/boot; \
		cp $(LIMINE_DIR)/BOOTX64.EFI $(TEST_WX_DIR)/esp_$$n/EFI/BOOT/BOOTX64.EFI; \
		cp limine.conf $(TEST_WX_DIR)/esp_$$n/limine.conf; \
		cp $(TEST_WX_DIR)/test_wx_$$n.elf $(TEST_WX_DIR)/esp_$$n/boot/kernel.elf; \
		cp $(TEST_WX_DIR)/ovmf_vars_src.fd $(TEST_WX_DIR)/ovmf_vars_$$n.fd; \
		( $(QEMU) -machine q35 -m 8G -cpu qemu64,+apic,+pdpe1gb \
			-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
			-drive if=pflash,format=raw,file=$(TEST_WX_DIR)/ovmf_vars_$$n.fd \
			-drive format=raw,file=fat:rw:$(TEST_WX_DIR)/esp_$$n \
			-serial file:$(TEST_WX_DIR)/serial_$$n.log -monitor none -no-reboot \
			-d int,cpu_reset -D $(TEST_WX_DIR)/qemu_$$n.log & ); \
		sleep 5; \
		pkill -f "qemu-system-x86_64.*esp_$$n" 2>/dev/null || true; \
		sleep 1; \
		echo "--- serial_$$n.log ---"; \
		cat $(TEST_WX_DIR)/serial_$$n.log; \
		echo; \
	done

# Host-side mannac lexer (docs/M2_PLAN.md stage 1) and its golden tests.
# Plain macOS clang, no freestanding/kernel flags - this runs on the host,
# not inside Deus.
MANNAC_DIR  := $(BUILD)/mannac
MANNAC_BIN  := $(MANNAC_DIR)/mannac
MANNAC_SRCS := tools/mannac/lexer.c tools/mannac/main.c

$(MANNAC_BIN): $(MANNAC_SRCS) tools/mannac/lexer.h
	@mkdir -p $(MANNAC_DIR)
	clang -std=c11 -Wall -Wextra -O2 -g -o $@ $(MANNAC_SRCS)

test-lexer: $(MANNAC_BIN)
	@./tools/run_lexer_goldens.sh $(MANNAC_BIN)

MANNAC_ASAN_BIN := $(MANNAC_DIR)/mannac-asan

$(MANNAC_ASAN_BIN): $(MANNAC_SRCS) tools/mannac/lexer.h
	@mkdir -p $(MANNAC_DIR)
	clang -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $@ $(MANNAC_SRCS)

test-lexer-asan: $(MANNAC_ASAN_BIN)
	@./tools/run_lexer_goldens.sh $(MANNAC_ASAN_BIN)

# Arena allocator unit test (plain + ASan/UBSan). Not a golden-file test -
# the arena has no stage output to diff, just invariants to assert.
ARENA_TEST_DIR  := $(BUILD)/arena_test
ARENA_TEST_SRCS := tools/mannac/arena.c tools/mannac/arena_test.c

test-arena:
	@mkdir -p $(ARENA_TEST_DIR)
	clang -std=c11 -Wall -Wextra -O2 -g -o $(ARENA_TEST_DIR)/arena_test $(ARENA_TEST_SRCS)
	@./tools/run_arena_test.sh $(ARENA_TEST_DIR)/arena_test

test-arena-asan:
	@mkdir -p $(ARENA_TEST_DIR)
	clang -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(ARENA_TEST_DIR)/arena_test-asan $(ARENA_TEST_SRCS)
	@./tools/run_arena_test.sh $(ARENA_TEST_DIR)/arena_test-asan

# AST node types + pretty-printer unit test (plain + ASan/UBSan). Also not
# golden-file based - asserts the printer's output against docs/M2_PLAN.md's
# documented example directly, byte-for-byte.
AST_TEST_DIR  := $(BUILD)/ast_test
AST_TEST_SRCS := tools/mannac/arena.c tools/mannac/ast.c tools/mannac/ast_test.c

test-ast:
	@mkdir -p $(AST_TEST_DIR)
	clang -std=c11 -Wall -Wextra -O2 -g -o $(AST_TEST_DIR)/ast_test $(AST_TEST_SRCS)
	$(AST_TEST_DIR)/ast_test

test-ast-asan:
	@mkdir -p $(AST_TEST_DIR)
	clang -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(AST_TEST_DIR)/ast_test-asan $(AST_TEST_SRCS)
	$(AST_TEST_DIR)/ast_test-asan

# Diagnostics + shared parser-state (depth bound, error cap, unified
# arena/depth/error-cap escape point) unit test (plain + ASan/UBSan).
PARSER_STATE_TEST_DIR  := $(BUILD)/parser_state_test
PARSER_STATE_TEST_SRCS := tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
                          tools/mannac/parser_state.c tools/mannac/parser_state_test.c

test-parser-state:
	@mkdir -p $(PARSER_STATE_TEST_DIR)
	clang -std=c11 -Wall -Wextra -O2 -g -o $(PARSER_STATE_TEST_DIR)/parser_state_test $(PARSER_STATE_TEST_SRCS)
	$(PARSER_STATE_TEST_DIR)/parser_state_test

test-parser-state-asan:
	@mkdir -p $(PARSER_STATE_TEST_DIR)
	clang -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(PARSER_STATE_TEST_DIR)/parser_state_test-asan $(PARSER_STATE_TEST_SRCS)
	$(PARSER_STATE_TEST_DIR)/parser_state_test-asan

# Direct-assertion unit tests for fixes that a golden-dump test can't
# reach (a synthetic multi-line Token; the `parenthesized` AST flag,
# which has no dump syntax since there's no Paren node; a lexer-error
# first token, which parser_dump's own one-token peek always routes to
# expression mode, never parser_parse_file - see parser_fixes_test.c's
# own top comment).
PARSER_FIXES_TEST_DIR  := $(BUILD)/parser_fixes_test
PARSER_FIXES_TEST_SRCS := tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
                          tools/mannac/parser_state.c tools/mannac/parser.c \
                          tools/mannac/lexer.c tools/mannac/parser_fixes_test.c

test-parser-fixes:
	@mkdir -p $(PARSER_FIXES_TEST_DIR)
	clang -std=c11 -Wall -Wextra -O2 -g -o $(PARSER_FIXES_TEST_DIR)/parser_fixes_test $(PARSER_FIXES_TEST_SRCS)
	@# explicit 8 MB stack: the E0111 tests claim "survives an 8 MB stack" (docs/M2_PLAN.md, Input contract)
	sh -c 'ulimit -s 8192 && $(PARSER_FIXES_TEST_DIR)/parser_fixes_test'

test-parser-fixes-asan:
	@mkdir -p $(PARSER_FIXES_TEST_DIR)
	clang -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(PARSER_FIXES_TEST_DIR)/parser_fixes_test-asan $(PARSER_FIXES_TEST_SRCS)
	sh -c 'ulimit -s 8192 && $(PARSER_FIXES_TEST_DIR)/parser_fixes_test-asan'

# Parser golden tests (plain + ASan/UBSan) - docs/M2_PLAN.md's "Golden test
# format for ASTs". parser_dump is a test-only harness (tools/mannac/
# parser_main.c), not the real `manna` CLI, same role here that main.c's
# lexer dump plays for the lexer. parser_dump parses a whole file,
# a block, or a single expression - parser_main.c picks the mode by
# peeking the first token.
PARSER_DUMP_DIR  := $(BUILD)/parser_dump
PARSER_DUMP_SRCS := tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
                     tools/mannac/parser_state.c tools/mannac/parser.c \
                     tools/mannac/lexer.c tools/mannac/parser_main.c

test-parser:
	@mkdir -p $(PARSER_DUMP_DIR)
	clang -std=c11 -Wall -Wextra -O2 -g -o $(PARSER_DUMP_DIR)/parser_dump $(PARSER_DUMP_SRCS)
	@./tools/run_parser_tests.sh $(PARSER_DUMP_DIR)/parser_dump

test-parser-asan:
	@mkdir -p $(PARSER_DUMP_DIR)
	clang -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(PARSER_DUMP_DIR)/parser_dump-asan $(PARSER_DUMP_SRCS)
	@./tools/run_parser_tests.sh $(PARSER_DUMP_DIR)/parser_dump-asan

# Step 11: determinism (docs/M2_PLAN.md S5) - same input must produce
# byte-identical output across two invocations of the same binary, and
# across plain vs ASan builds, for both implemented stages (lexer,
# parser). Rebuilds all four binaries (reusing the exact same sources/
# flags as test-lexer/-asan and test-parser/-asan) so this target has no
# ordering dependency on those having already run.
test-determinism:
	@mkdir -p $(MANNAC_DIR) $(PARSER_DUMP_DIR)
	clang -std=c11 -Wall -Wextra -O2 -g -o $(MANNAC_BIN) $(MANNAC_SRCS)
	clang -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(MANNAC_ASAN_BIN) $(MANNAC_SRCS)
	clang -std=c11 -Wall -Wextra -O2 -g -o $(PARSER_DUMP_DIR)/parser_dump $(PARSER_DUMP_SRCS)
	clang -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(PARSER_DUMP_DIR)/parser_dump-asan $(PARSER_DUMP_SRCS)
	@mkdir -p $(IR_DUMP_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -O2 -g -o $(IR_DUMP_DIR)/ir_dump $(IR_DUMP_SRCS)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(IR_DUMP_DIR)/ir_dump-asan $(IR_DUMP_SRCS)
	@./tools/run_determinism_test.sh $(MANNAC_BIN) $(MANNAC_ASAN_BIN) $(PARSER_DUMP_DIR)/parser_dump $(PARSER_DUMP_DIR)/parser_dump-asan $(IR_DUMP_DIR)/ir_dump $(IR_DUMP_DIR)/ir_dump-asan

# Typed IR (docs/history/TYPED_IR.md step 3). The IR targets honor MANNAC_CC so a
# Linux/gcc reviewer run is `make MANNAC_CC=gcc test-ir-unit-asan` (the older
# targets above hardcode clang and are left as they are). ir_test.c runs under
# an 8 MB stack, the host default the stack tests are about, and the stack
# shapes run again at 2 MB: the dumper must keep a 4x margin on the maximal
# (512-high) trees at that limit, plain and ASan.
MANNAC_CC      ?= clang
IR_TEST_DIR    := $(BUILD)/ir_test
IR_TEST_SRCS   := tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
                  tools/mannac/ir.c tools/mannac/ir_dump.c tools/mannac/ir_build.c tools/mannac/ir_decls.c tools/mannac/ir_expr.c tools/mannac/ir_body.c tools/mannac/ir_calls.c tools/mannac/ir_names.c tools/mannac/ir_rename.c \
                  tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c \
                  tools/mannac/ir_test.c

# ir_dump is the test-only golden harness (tools/mannac/ir_main.c), the IR
# counterpart of parser_dump: tests/ir/*.mn -> *.ir, plus a sweep of every
# tests/parser input for crashes (tools/run_ir_tests.sh).
IR_DUMP_DIR  := $(BUILD)/ir_dump
IR_DUMP_SRCS := tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
                tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c \
                tools/mannac/ir.c tools/mannac/ir_dump.c tools/mannac/ir_build.c tools/mannac/ir_decls.c tools/mannac/ir_expr.c tools/mannac/ir_body.c tools/mannac/ir_calls.c tools/mannac/ir_names.c tools/mannac/ir_rename.c \
                tools/mannac/ir_main.c

test-ir:
	@mkdir -p $(IR_DUMP_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -O2 -g -o $(IR_DUMP_DIR)/ir_dump $(IR_DUMP_SRCS)
	@./tools/run_ir_tests.sh $(IR_DUMP_DIR)/ir_dump

test-ir-asan:
	@mkdir -p $(IR_DUMP_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(IR_DUMP_DIR)/ir_dump-asan $(IR_DUMP_SRCS)
	@./tools/run_ir_tests.sh $(IR_DUMP_DIR)/ir_dump-asan

# Typed-IR step 14: every rename fix applied to tests/ir_fix/*.mn must give tests/ir_fix/*.fixed.mn, and the
# fixed source must re-check clean (or give *.remaining); tools/run_ir_fix_tests.sh.
test-ir-fix:
	@mkdir -p $(IR_DUMP_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -O2 -g -o $(IR_DUMP_DIR)/ir_dump $(IR_DUMP_SRCS)
	@./tools/run_ir_fix_tests.sh $(IR_DUMP_DIR)/ir_dump

# Typed-IR step 18: every E0261 fix-it, over operand type pairs x operators x contexts x nesting shapes (~474,000 programs, ~10 s): each offered
# fix is one lossless wrap and re-checks clean (tools/mannac/ir_e0261_exhaustive.c).
IR_EXH_SRCS := tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
               tools/mannac/ir.c tools/mannac/ir_dump.c tools/mannac/ir_build.c tools/mannac/ir_decls.c tools/mannac/ir_expr.c tools/mannac/ir_body.c tools/mannac/ir_calls.c tools/mannac/ir_names.c tools/mannac/ir_rename.c \
               tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c \
               tools/mannac/ir_e0261_exhaustive.c

test-e0261-exhaustive:
	@mkdir -p $(IR_TEST_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -O2 -g -o $(IR_TEST_DIR)/ir_e0261_exhaustive $(IR_EXH_SRCS)
	$(IR_TEST_DIR)/ir_e0261_exhaustive

# Typed-IR step 15: every E02xx code docs/error_codes.md calls emitted has a golden or unit test (and a constructor in
# diag.c, and nothing emitted is undocumented); prints the per-code table.
test-ir-error-codes:
	@python3 tools/check_error_code_coverage.py

# Typed-IR step 15: every poisoning line of the builder (ir_error / is_error / error_node / ir_type_error / the struct
# `poisoned` flag, in ir_decls/expr/body/calls/build/rename.c) is RUN by at least one tests/ir golden holding exactly ONE
# diagnostic (per-golden source coverage; needs Xcode's llvm-cov). Prints the table; fails on a line no such golden runs.
# This is not a proof that the path cannot cascade, only that a one-diagnostic test exercises it.
audit-ir-poison:
	@python3 tools/audit_ir_poison.py --check

test-ir-fix-asan:
	@mkdir -p $(IR_DUMP_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -o $(IR_DUMP_DIR)/ir_dump-asan $(IR_DUMP_SRCS)
	@./tools/run_ir_fix_tests.sh $(IR_DUMP_DIR)/ir_dump-asan

test-ir-unit:
	@mkdir -p $(IR_TEST_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -O2 -g -pthread -o $(IR_TEST_DIR)/ir_test $(IR_TEST_SRCS)
	sh -c 'ulimit -s 8192 && $(IR_TEST_DIR)/ir_test'
	sh -c 'ulimit -s 2048 && $(IR_TEST_DIR)/ir_test --stack'
	sh -c 'ulimit -s 2048 && $(IR_TEST_DIR)/ir_test --build-stack'

# The IR with asserts compiled out (-DNDEBUG): the code after the two asserts that replaced IR_UNIMPLEMENTED raisers
# must still stop the build (tools/mannac/ir_ndebug_test.c). Uses the IR sources only, no ir_test.c.
IR_NDEBUG_SRCS := tools/mannac/arena.c tools/mannac/ast.c tools/mannac/diag.c \
                  tools/mannac/ir.c tools/mannac/ir_dump.c tools/mannac/ir_build.c tools/mannac/ir_decls.c tools/mannac/ir_expr.c tools/mannac/ir_body.c tools/mannac/ir_calls.c tools/mannac/ir_names.c tools/mannac/ir_rename.c \
                  tools/mannac/parser_state.c tools/mannac/parser.c tools/mannac/lexer.c \
                  tools/mannac/ir_ndebug_test.c

test-ir-ndebug:
	@mkdir -p $(IR_TEST_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -O2 -DNDEBUG -o $(IR_TEST_DIR)/ir_ndebug_test $(IR_NDEBUG_SRCS)
	$(IR_TEST_DIR)/ir_ndebug_test

test-ir-unit-asan:
	@mkdir -p $(IR_TEST_DIR)
	$(MANNAC_CC) -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -pthread -o $(IR_TEST_DIR)/ir_test-asan $(IR_TEST_SRCS)
	sh -c 'ulimit -s 8192 && $(IR_TEST_DIR)/ir_test-asan'
	sh -c 'ulimit -s 2048 && $(IR_TEST_DIR)/ir_test-asan --stack'
	sh -c 'ulimit -s 2048 && $(IR_TEST_DIR)/ir_test-asan --build-stack'

# Per-file coverage for mannac (lexer, arena, ast, diag, parser_state, ir*),
# via clang's source-based coverage (llvm-profdata/llvm-cov, bundled with
# Xcode - no brew dependency). Rebuilds each test binary instrumented
# (-fprofile-instr-generate -fcoverage-mapping), runs it with a per-process
# profile filename (%p) since several tests exec their binary more than
# once, merges every .profraw into one profile, then reports library
# source files only - *_test.c and main.c are excluded since their own
# coverage isn't the question `make coverage` is answering.
COVERAGE_DIR   := $(BUILD)/coverage
COVERAGE_FLAGS := -fprofile-instr-generate -fcoverage-mapping

coverage:
	@mkdir -p $(COVERAGE_DIR)
	@# stale profiles from an earlier run would be merged into this one's numbers
	find $(COVERAGE_DIR) -maxdepth 1 -type f -name '*.profraw' -delete
	rm -f $(COVERAGE_DIR)/merged.profdata
	clang -std=c11 -Wall -Wextra -g $(COVERAGE_FLAGS) -o $(COVERAGE_DIR)/mannac $(MANNAC_SRCS)
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/lexer-%p.profraw ./tools/run_lexer_goldens.sh $(COVERAGE_DIR)/mannac
	clang -std=c11 -Wall -Wextra -g $(COVERAGE_FLAGS) -o $(COVERAGE_DIR)/arena_test $(ARENA_TEST_SRCS)
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/arena-%p.profraw ./tools/run_arena_test.sh $(COVERAGE_DIR)/arena_test
	clang -std=c11 -Wall -Wextra -g $(COVERAGE_FLAGS) -o $(COVERAGE_DIR)/ast_test $(AST_TEST_SRCS)
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/ast-%p.profraw $(COVERAGE_DIR)/ast_test
	clang -std=c11 -Wall -Wextra -g $(COVERAGE_FLAGS) -o $(COVERAGE_DIR)/parser_state_test $(PARSER_STATE_TEST_SRCS)
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/parser_state-%p.profraw $(COVERAGE_DIR)/parser_state_test
	clang -std=c11 -Wall -Wextra -g $(COVERAGE_FLAGS) -o $(COVERAGE_DIR)/parser_fixes_test $(PARSER_FIXES_TEST_SRCS)
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/parser_fixes-%p.profraw $(COVERAGE_DIR)/parser_fixes_test
	clang -std=c11 -Wall -Wextra -g $(COVERAGE_FLAGS) -o $(COVERAGE_DIR)/parser_dump $(PARSER_DUMP_SRCS)
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/parser-%p.profraw ./tools/run_parser_tests.sh $(COVERAGE_DIR)/parser_dump
	clang -std=c11 -Wall -Wextra -g $(COVERAGE_FLAGS) -pthread -o $(COVERAGE_DIR)/ir_test $(IR_TEST_SRCS)
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/ir_test-%p.profraw sh -c 'ulimit -s 8192 && $(COVERAGE_DIR)/ir_test'
	clang -std=c11 -Wall -Wextra -g $(COVERAGE_FLAGS) -o $(COVERAGE_DIR)/ir_dump $(IR_DUMP_SRCS)
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/ir-%p.profraw ./tools/run_ir_tests.sh $(COVERAGE_DIR)/ir_dump
	LLVM_PROFILE_FILE=$(COVERAGE_DIR)/ir_fix-%p.profraw ./tools/run_ir_fix_tests.sh $(COVERAGE_DIR)/ir_dump
	xcrun llvm-profdata merge -sparse $(COVERAGE_DIR)/*.profraw -o $(COVERAGE_DIR)/merged.profdata
	xcrun llvm-cov report \
		$(COVERAGE_DIR)/mannac \
		-object $(COVERAGE_DIR)/arena_test \
		-object $(COVERAGE_DIR)/ast_test \
		-object $(COVERAGE_DIR)/parser_state_test \
		-object $(COVERAGE_DIR)/parser_fixes_test \
		-object $(COVERAGE_DIR)/parser_dump \
		-object $(COVERAGE_DIR)/ir_test \
		-object $(COVERAGE_DIR)/ir_dump \
		-instr-profile=$(COVERAGE_DIR)/merged.profdata \
		-ignore-filename-regex='(_test\.c|main\.c)$$'

# Step 12: libFuzzer harnesses (tools/mannac/fuzz_lexer.c, fuzz_parser.c).
# Deliberately NOT part of `test-*`/CI - needs brew's keg-only llvm (Apple's
# Xcode clang has no libFuzzer runtime; see tools/fuzz/run_fuzzers.sh), and
# runs for a fixed wall-clock budget rather than to a pass/fail verdict.
# DURATION_LEXER/DURATION_PARSER (seconds) and LLVM_CLANG are overridable,
# e.g. `make fuzz DURATION_PARSER=1800`.
fuzz:
	./tools/fuzz/run_fuzzers.sh both

fuzz-lexer:
	./tools/fuzz/run_fuzzers.sh lexer

fuzz-parser:
	./tools/fuzz/run_fuzzers.sh parser

fuzz-ir:
	./tools/fuzz/run_fuzzers.sh ir

fuzz-ir-ungated:
	./tools/fuzz/run_fuzzers.sh ir-ungated

clean:
	rm -rf $(BUILD) $(IMAGE) serial.log qemu.log

# Typed-IR step 2b: where does the AST printer fail on an 8 MB stack, per chain
# shape, plain and ASan (the measurement behind MAX_EXPR_HEIGHT, ast.h). Runs
# for ~1-2 minutes; deliberately NOT part of `test-*`/CI.
measure-height:
	./tools/measure_expr_height.sh

# Typed-IR step 3b: per-shape stack the IR dumper needs on a maximal tree, and
# the 4x gate (tools/measure_ir_stack.sh). Not part of `test-*`/CI.
measure-ir-stack:
	CC=$(MANNAC_CC) ./tools/measure_ir_stack.sh

# Typed-IR step 6c: per-level stack cost of the real builder on maximal parser-built trees
# and the 4x gate (tools/measure_ir_build_stack.sh). Not part of `test-*`/CI.
measure-ir-build-stack:
	CC=$(MANNAC_CC) ./tools/measure_ir_build_stack.sh
