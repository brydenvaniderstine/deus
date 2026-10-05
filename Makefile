
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

.PHONY: all image run debug clean limine stage-esp test-wx

all: $(KERNEL_ELF)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/%.o: %.S
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

clean:
	rm -rf $(BUILD) $(IMAGE) serial.log qemu.log
