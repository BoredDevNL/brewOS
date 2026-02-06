# BrewOS Makefile
# Target Architecture: x86_64
# Host: macOS

# Toolchain Definitions
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
NASM = nasm
XORRISO = xorriso

# Directories
SRC_DIR = src/kernel
BUILD_DIR = build
ISO_DIR = iso_root

# Output
KERNEL_ELF = $(BUILD_DIR)/brewos.elf
ISO_IMAGE = brewos.iso

# Source Files
C_SOURCES = $(wildcard $(SRC_DIR)/*.c)
CLI_APP_SOURCES = $(wildcard $(SRC_DIR)/cli_apps/*.c)
ASM_SOURCES = $(wildcard $(SRC_DIR)/*.asm)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES)) \
            $(patsubst $(SRC_DIR)/cli_apps/%.c, $(BUILD_DIR)/cli_apps/%.o, $(CLI_APP_SOURCES)) \
            $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))

# Flags
CFLAGS = -g -O2 -pipe -Wall -Wextra -std=gnu11 -ffreestanding \
         -fno-stack-protector -fno-stack-check -fno-lto -fPIE \
         -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
         -I$(SRC_DIR) -I$(SRC_DIR)/cli_apps

LDFLAGS = -m elf_x86_64 -nostdlib -static -pie --no-dynamic-linker \
          -z text -z max-page-size=0x1000 -T linker.ld

NASMFLAGS = -f elf64

# Limine Version
LIMINE_VERSION = 7.0.0
LIMINE_URL_BASE = https://github.com/limine-bootloader/limine/raw/v$(LIMINE_VERSION)

.PHONY: all clean run limine-setup

all: $(ISO_IMAGE)

# Ensure build directories exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/cli_apps

# Download Limine Binaries via Git
limine-setup:
	@if [ ! -f limine/limine-bios.sys ]; then \
		echo "Limine binaries missing or invalid. Cloning v$(LIMINE_VERSION)-binary..."; \
		rm -rf limine; \
		git clone https://github.com/limine-bootloader/limine.git --branch=v$(LIMINE_VERSION)-binary --depth=1 limine; \
	fi
	@if [ ! -f $(SRC_DIR)/limine.h ]; then \
		echo "Copying limine.h..."; \
		cp limine/limine.h $(SRC_DIR)/limine.h; \
	fi
	@echo "Building Limine host utility..."; \
	$(MAKE) -C limine

# Compile C Sources
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) limine-setup
	$(CC) $(CFLAGS) -c $< -o $@

# Compile CLI Apps C Sources
$(BUILD_DIR)/cli_apps/%.o: $(SRC_DIR)/cli_apps/%.c | $(BUILD_DIR) limine-setup
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble ASM Sources
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

# Link Kernel
$(KERNEL_ELF): $(OBJ_FILES)
	$(LD) $(LDFLAGS) -o $@ $(OBJ_FILES)

# Create ISO
$(ISO_IMAGE): $(KERNEL_ELF) limine.cfg limine-setup
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)
	mkdir -p $(ISO_DIR)/EFI/BOOT
	
	# Copy Kernel and Config
	cp $(KERNEL_ELF) $(ISO_DIR)/
	cp limine.cfg $(ISO_DIR)/
	
	# Copy README
	cp README.md $(ISO_DIR)/

	# Copy user file.c if it exists
	@if [ -f file.c ]; then cp file.c $(ISO_DIR)/; fi
	
	# Copy Wallpaper (if it exists)
	@if [ -f src/kernel/wallpaper.ppm ]; then cp src/kernel/wallpaper.ppm $(ISO_DIR)/; fi
	
	# Copy Limine Bootloader Files (flat structure in binary branch)
	cp limine/limine-bios.sys $(ISO_DIR)/
	cp limine/limine-bios-cd.bin $(ISO_DIR)/
	cp limine/limine-uefi-cd.bin $(ISO_DIR)/
	
	# Create EFI Boot Files
	cp limine/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	cp limine/BOOTIA32.EFI $(ISO_DIR)/EFI/BOOT/
	
	# Generate ISO
	$(XORRISO) -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_IMAGE)
	
	# Install Limine to ISO (for BIOS boot)
	./limine/limine bios-install $(ISO_IMAGE)

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO_IMAGE)

run: $(ISO_IMAGE)
	qemu-system-x86_64 -m 2G -serial stdio -cdrom $(ISO_IMAGE) -boot d \
		-audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 \
		-netdev user,id=net0 -device e1000,netdev=net0
