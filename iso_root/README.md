# Brew OS 1.01 Alpha

## Brewkernel is now BrewOS!
Brewkernel will from now on be deprecated as it's core became too messy. I have built a less bloated kernel and wrote a DE above it, which is why it is now an OS instead of a kernel (in my opinion).


<img src="asciiart.png" width="200" /> </br>
Brew Kernel is a simple x86_64 hobbyist operating system. 
It features a DE (and WM), a FAT32 filesystem, customizable UI and much much more!

## Features
- Brew WM
- Fat 32 FS
- 64-bit long mode support
- Multiboot2 compliant
- Text editor
- IDT
- Ability to run on actual x86_64 hardware
- CLI

## Prerequisites

To build BrewOS, you'll need the following tools installed:

- **x86_64 ELF Toolchain**: `x86_64-elf-gcc`, `x86_64-elf-ld`
- **NASM**: Netwide Assembler for compiling assembly code
- **xorriso**: For creating bootable ISO images
- **QEMU** (optional): For testing the kernel in an emulator

On macOS, you can install these using Homebrew:
```sh
brew install x86_64-elf-binutils x86_64-elf-gcc nasm xorriso qemu
```

## Building

Simply run `make` from the project root:

```sh
make
```

This will:
1. Compile all kernel C sources and assembly files
2. Link the kernel ELF binary
3. Generate a bootable ISO image (`brewos.iso`)

The build output is organized as follows:
- Compiled object files: `build/`
- ISO root filesystem: `iso_root/`
- Final ISO image: `brewos.iso`

## Running

### QEMU Emulation

Run the kernel in QEMU:

```sh
make run
```

Or manually:
```sh
qemu-system-x86_64 -m 2G -serial stdio -cdrom brewos.iso -boot d
```

### Running on Real Hardware

*Warning: This is at YOUR OWN RISK. This software comes with ZERO warranty and may break your system.*

1. **Create bootable USB**: Use [Balena Etcher](https://www.balena.io/etcher/) to flash `brewos.iso` to a USB drive

2. **Prepare the system**:
   - Enable legacy (BIOS) boot in your system BIOS/UEFI settings
   - Disable Secure Boot if needed

3. **Boot**: Insert the USB drive and select it in the boot menu during startup

4. **Tested Hardware**:
   - HP EliteDesk 705 G4 DM (AMD Ryzen 5 PRO 2400G, Radeon Vega)
   - Lenovo ThinkPad A475 20KL002VMH (AMD Pro A12-8830B, Radeon R7)


## Project Structure

- `src/kernel/` - Main kernel implementation
  - `boot.asm` - Boot assembly code
  - `main.c` - Kernel entry point
  - `*.c / *.h` - Core kernel modules (graphics, interrupts, filesystem, etc.)
  - `cli_apps/` - Command-line applications
  - `wallpaper.ppm` - Default desktop wallpaper
- `build/` - Compiled object files (generated during build)
- `iso_root/` - ISO filesystem layout (generated during build)
- `limine/` - Limine bootloader files (downloaded automatically)
- `linker.ld` - Linker script for x86_64 ELF
- `limine.cfg` - Limine bootloader configuration
- `Makefile` - Build configuration and targets




###
###

<h2 align="left">Help me brew some coffee! ☕️</h2>

###

<p align="left">
  If you enjoy this project, and like what i'm doing here, consider buying me a coffee!
  <br><br>
  <a href="https://buymeacoffee.com/boreddevnl" target="_blank">
    <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="50" style="border-radius: 8px;" />
  </a>
</p>

###


## License

Copyright (C) 2024-2026 boreddevnl

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

NOTICE
------

This product includes software developed by Chris ("boreddevnl") as part of the BrewKernel project.

Copyright (C) 2024–2026 Chris / boreddevnl (previously boreddevhq)

All source files in this repository contain copyright and license
headers that must be preserved in redistributions and derivative works.

If you distribute or modify this project (in whole or in part),
you MUST:

  - Retain all copyright and license headers at the top of each file.
  - Include this NOTICE file along with any redistributions or
    derivative works.
  - Provide clear attribution to the original author in documentation
    or credits where appropriate.

The above attribution requirements are informational and intended to
ensure proper credit is given. They do not alter or supersede the
terms of the GNU General Public License (GPL), which governs this work.
