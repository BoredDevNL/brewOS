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
#ifndef APPS_NERD_H
#define APPS_NERD_H

#include "../io.h"
#include "../print.h"

static void nerd() {
    brew_str("\n");
    brew_str("You read the manual? NERD. you know what?\n");
    brew_str("Fuck you.\n");
    brewing(5000000000);
    

    // Set the PIT to the desired frequency (1000 Hz for high pitch)
    outb(0x43, 0xB6);  // Command byte: channel 2, mode 3, binary
    int frequency = 1000;
    int divisor = 1193180 / frequency;
    outb(0x42, divisor & 0xFF);
    outb(0x42, (divisor >> 8) & 0xFF);

    outb(0x61, inb(0x61) | 0x03);   
     
    for(int i = 0; i < 1000000000000000000; i++) {  
        print_set_color(PRINT_INDEX_0, PRINT_INDEX_15); // black on white
        print_clear();
        brewing(500000); 
        
        print_set_color(PRINT_INDEX_15, PRINT_INDEX_0); // white on black
        print_clear();
        brewing(500000);
    }

}

#endif // APPS_NERD_H
