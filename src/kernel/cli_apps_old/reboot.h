/*
 * Brew Kernel
 * Copyright (C) 2024-2026 boreddevnl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef REBOOT_H
#define REBOOT_H

#include "../print.h"
#include "../io.h"

void brewing(int iterations);

void reboot_command() {
    brew_str("\nInitiating system reboot...\n");
    brewing(10000000);  
    while ((inb(0x64) & 2) != 0) {
        brewing(1000);
    }  
    outb(0x64, 0xFE); 
    brewing(50000000);   
    asm volatile ("lidt %0" : : "m"(*(char*)0)); 
    asm volatile ("int $0x3");  
    brew_str("WARNING: System reboot failed.\n");
    brew_str("Please reset your computer manually.\n");
}

#endif