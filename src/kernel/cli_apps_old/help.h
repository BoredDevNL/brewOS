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
#ifndef APPS_HELP_H
#define APPS_HELP_H

#include "../print.h"

static void display_help() {
    brew_str("\nAvailable commands:\n");
    brew_str("  HELP    - Display this help message\n");
    brew_str("  DATE    - Display the current date and time\n");
    brew_str("  EXIT    - Exit CLI mode and return to regular typing\n");
    brew_str("  CLEAR   - Clear the screen\n");
    brew_str("  ABOUT   - Display system information\n");
    brew_str("  MATH    - Perform basic arithmetic\n");
    brew_str("  MAN     - Show the detailed user manual\n");
    brew_str("  LICENSE - Display the GNU GPLv3 license\n");
    brew_str("  UPTIME  - Show how long the system has been running\n");
    brew_str("  MEMORY  - Display memory usage statistics\n");
    brew_str("  BEEP    - Makes a beep sound using the PC speaker\n");
    brew_str("  TXTEDIT - Open the text editor\n");
    brew_str("  COWSAY. - MOO!\n");
    brew_str("  LS      - List files in current directory\n");
    brew_str("  CD      - Change current directory\n");
    brew_str("  PWD     - Print working directory\n");
    brew_str("  MKDIR   - Create one or more directories\n");
    brew_str("  RM      - Remove a file or empty directory\n");
    brew_str("  CAT     - Display file contents\n");
    brew_str("  TOUCH   - Create an empty file\n");
    brew_str("  ECHO    - Print text (can redirect to file with >)\n");
    brew_str("  NETINIT - Initialize network card\n");
    brew_str("  NETINFO - Show network status (MAC, IP)\n");
    brew_str("  UDPTEST - Start UDP echo server on port 12345 (broken)\n");
    brew_str("  UDPSEND - Send UDP packet (UDPSEND <ip> <port> <msg>)\n");
    brew_str("\nPipe support:\n");
    brew_str("  CAT <file> | UDPSEND <ip> <port> - Send file contents via UDP\n");
}

#endif // APPS_HELP_H
