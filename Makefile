# Makefile for the SPL - a bootloader for the QSD8250 in the HTC HD2.
#
# Built with the host's clang and lld, cross-compiling for bare-metal ARM: no
# arm-none-eabi toolchain, no libc.  "make" builds spl.bin, the flat image the
# boot ROM copies to physical 0 and jumps to.
#
#   make            build build/spl.bin and report its size
#   make run        boot it in the HD2 emulator against images/nand.img
#   make dis        disassemble it
#   make clean

# make predefines CC as "cc", so ?= would never take: override the built-in
# default only, and still let "make CC=..." win.
ifeq ($(origin CC),default)
CC := clang
endif
OBJCOPY := llvm-objcopy
OBJDUMP := llvm-objdump
SIZE    := llvm-size

TARGET  := arm-none-eabi
ARCH    := -mcpu=cortex-a8 -marm -mfloat-abi=soft

CFLAGS  := --target=$(TARGET) $(ARCH) \
           -ffreestanding -fno-builtin -nostdlib -nostdinc \
           -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables \
           -std=gnu11 -O2 -g \
           -Wall -Wextra -Wundef -Wshadow -Wvla \
           -ffunction-sections -fdata-sections \
           -I. -Iinclude -Ispl $(addprefix -I,$(wildcard drivers/*))

BUILD   := build
BIN     := $(BUILD)/spl

LDFLAGS := --target=$(TARGET) -fuse-ld=lld -nostdlib -static \
           -T linker/spl.ld -Wl,--gc-sections -Wl,--build-id=none \
           -Wl,-Map=$(BUILD)/spl.map

# Everything in spl/ and one directory down in drivers/ is built; there is no
# list here to keep in step with the tree.
SRCS := $(sort $(wildcard spl/*.c) $(wildcard drivers/*/*.c))

OBJS := $(addprefix $(BUILD)/,$(SRCS:.c=.o))
DEPS := $(OBJS:.o=.d)

# The emulator this is meant to run on, for "make run".  SPL=<file> hands the
# core to this bootloader instead of letting the machine stand in for one.
QEMU_DIR ?= ../qemu-hd2
QEMU     ?= $(QEMU_DIR)/src/qemu/build/qemu-system-arm
NAND     ?= $(QEMU_DIR)/images/nand.img

.PHONY: all clean run dis size

all: $(BIN).bin size

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BIN).elf: $(OBJS) linker/spl.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BIN).bin: $(BIN).elf
	$(OBJCOPY) -O binary $< $@
	@echo "built $@, $$($(SIZE) -t $< | tail -1)"

size: $(BIN).elf
	@$(SIZE) $<

run: all
	$(QEMU) -L $(QEMU_DIR)/src/qemu/pc-bios \
	       -M htcleo,spl=$(CURDIR)/$(BIN).bin \
	       -drive if=mtd,format=raw,file=$(NAND) \
	       -serial stdio

dis: $(BIN).elf
	$(OBJDUMP) -d --no-show-raw-insn $<

clean:
	rm -rf $(BUILD)

-include $(DEPS)