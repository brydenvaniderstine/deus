
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

C_SRCS := $(shell find kernel -name '*.c')
S_SRCS := $(shell find kernel -name '*.S')
OBJS   := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
          $(patsubst %.S,$(BUILD)/%.o,$(S_SRCS))

QEMU         := qemu-system-x86_64
OVMF_CODE    := $(shell brew --prefix qemu 2>/dev/null)/share/qemu/edk2-x86_64-code.fd
OVMF_VARS    := $(shell brew --prefix qemu 2>/dev/null)/share/qemu/edk2-i386-vars.fd

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

.PHONY: all image run debug clean limine stage-esp

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

clean:
	rm -rf $(BUILD) $(IMAGE) serial.log qemu.log
