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
#ifndef SHUTDOWN_H
#define SHUTDOWN_H

#include "../print.h"
#include "../io.h"

void brewing(int iterations);

void shutdown_command() {
    brew_str("\nInitiating system shutdown...\n");
    brewing(10000000); 
    outw(0xB004, 0x2000);
    outb(0x64, 0xFE);
        brew_str("WARNING: System shutdown failed.\n");
    brew_str("It is now safe to turn off your computer.\n");
}

#endif