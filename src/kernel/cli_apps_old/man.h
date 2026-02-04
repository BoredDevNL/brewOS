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
#ifndef APPS_MAN_H
#define APPS_MAN_H

#include "print.h"
#include "keyboard.h"

const char* manual_pages[] = {
    "BrewKernel User Manual",
    "----------------------",
    "",
    "Welcome to the BrewKernel, a simple hobby operating system kernel designed",
    "for x86_64 architecture. This manual provides an overview of the system,",
    "its features, and available commands.",
    "",
    "== System Overview ==",
    "BrewKernel boots into a VGA text mode display (80x25 characters). It",
    "initializes a custom color palette and provides basic keyboard input",
    "handling. The primary user interface is a simple command-line",
    "interface (CLI).",
    "",
    "== Features ==",
    "* Ramdisk-based Filesystem: A simple in-memory filesystem supporting",
    "  files and directories.",
    "* VGA Text Mode Driver: Full control over text and background colors.",
    "* PS/2 Keyboard Driver: Handles key presses, including modifier keys",
    "  like Shift.",
    "* Simple CLI: A basic shell to interact with the kernel.",
    "* Command History (sort of): The last entered command remains in the",
    "  buffer but is not yet a full history feature.",
    "",
    "== How to Use the CLI ==",
    "Upon boot, you can type 'CLI' and press Enter to start the command-line",
    "interface. Once in the CLI, you can type commands followed by Enter.",
    "Commands are case-insensitive.",
    "",
    "== Special Keys ==",
    "* Up/down arrow keys, scrolls through command history. (CLI mode only)",
    "",
    "== File System ==",
    "The BrewKernel includes a simple ramdisk-based filesystem. You can create,",
    "read, write, and list files and directories using the following commands:",
    "This filesystem will NOT save to disk and only saves to RAM.",
    "This filesystem is UNIX like, using '/' as the directory separator.",
    "Commands like ls, cd, mkdir work with absolute and relative paths.",
    "",
    "== Brew Language==",
    "== This version of brewkernel contains a simple interpreter for the",
    "== brew programming language. Currently the only implemented function is",
    "== brew_str, which prints a string to the screen. More features will be",
    "== added in future versions. Brew files have the extension .brew",
    "== you can run a brew file using the command: brewer >filename.brew<",
    "== Available Commands ==",
    "HELP: Displays a short list of available commands.",    
    "LS [path]: Lists files in the specified directory or current directory if",
    "          no path is given.",
    "",
    "CD [path]: Changes the current directory to 'path'.",
    "",
    "PWD: Prints the current working directory path.",
    "",
    "MKDIR [path]: Creates a new directory at the specified path.",
    "",
    "MAN: Shows this detailed user manual. Use UP/DOWN arrow keys to",
    "     scroll and 'q' to quit the manual viewer.",
    "",
    "ABOUT: Displays information about the kernel, including version, build",
    "       date, architecture, and compiler.",
    "",
    "MATH:",
    "A simple calculator for basic arithmetic operations",
    "(add, subtract, multiply, divide) on integers.",
  
    "",
    "DATE: Displays the current date and time, with an option to select your",
    "      timezone.",
    "",
    "TXTEDIT: A simple text editor. Features:",
    "      - Create and edit multiple text files",
    "      - Files are preserved between editor sessions (until reboot)",
    "      - Navigate with arrow keys",
    "      - Save/load files with custom names",
    "      - ESC to exit (with save prompt)",
    "USAGE: txtedit >filename< or:",
    "txtedit and choose name on save",
    "",
    "IREADTHEMANUAL: Wow. You actually read the manual. Run this command",
    "      for a special surprise!",
    "",
    "CLEAR: Clears the entire screen and moves the cursor to the top-left.",
    "",
    "EXIT: Exits the CLI mode and returns to the initial kernel screen.",
    "",
    "LICENSE: Displays the full GNU General Public License v3, under which",
    "         BrewKernel is distributed. Use UP/DOWN to scroll, 'q' to quit.",
    "",
    "COWSAY:  Moo! Displays a cow saying a message. Usage: COWSAY [message]",
    "         Inspired by GNU/LINUX",
    "",    
    "UPTIME: Shows how long the system has been running since boot.",
    "BEEP: Makes a beep sound using the PC speaker.",
    "--- End of Manual ---"
};
const int manual_num_lines = sizeof(manual_pages) / sizeof(char*);

static void show_manual() {
    int top_line = 0;
    int needs_redraw = 1;

    while (1) {
        if (needs_redraw) {
            print_clear();
            print_set_color(PRINT_INDEX_7, PRINT_INDEX_0);
            for (int i = 0; i < 24 && (top_line + i) < manual_num_lines; i++) {
                brew_str(manual_pages[top_line + i]);
                brew_str("\n");
            }
            print_set_color(PRINT_INDEX_15, PRINT_INDEX_9);
            brew_str("-- (Up/Down to scroll, 'q' to quit) --");
            print_set_color(PRINT_INDEX_7, PRINT_INDEX_0);
            needs_redraw = 0;
        }

        // Wait for a key press before doing anything
        while (!check_keyboard()) { /* Do nothing */ }

        unsigned char sc = read_scan_code(); // Now read the key
        if (sc == 0x48 && top_line > 0) { top_line--; needs_redraw = 1; } // Up Arrow
        else if (sc == 0x50 && (top_line + 24) < manual_num_lines) { top_line++; needs_redraw = 1; } // Down Arrow
        else if (scan_code_to_ascii(sc) == 'q') { 
            print_set_color(PRINT_INDEX_7, PRINT_INDEX_0);
            break; 
        }

        brewing(10000000); // Delay to handle keyboard typematic rate (key-repeat)
    }
}


#endif // APPS_MAN_H
