/*
 * Brew Kernel Cowsay Utility
 * Copyright (C) 2024-2026 boreddevnl
 * 
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
//
// Implementation of cowsay from GNU/LINUX for Brew Kernel.
// Props to Tony Monroe for the original concept.
// One of these days we WONT port it to Windows. :3
//
#ifndef APPS_COWSAY_H
#define APPS_COWSAY_H

#include "print.h"

// Function to calculate string length since we don't have string.h
static int brew_strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

// String comparison for parsing command
static int strncmp_kernel(const char* s1, const char* s2, int n) {
    while (n-- > 0) {
        if (*s1 != *s2) {
            return (*s1 - *s2);
        }
        if (*s1 == '\0') {
            return 0;
        }
        s1++;
        s2++;
    }
    return 0;
}

// Function to find where the command arguments start
static const char* find_args(const char* cmd) {
    while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
    return cmd;
}

// Function to draw the top border of the speech bubble
static void draw_top_border(int width) {
    brew_str(" ");
    for (int i = 0; i < width + 2; i++) {
        brew_str("_");
    }
    brew_str("\n");
}

// Function to draw the bottom border of the speech bubble
static void draw_bottom_border(int width) {
    brew_str(" ");
    for (int i = 0; i < width + 2; i++) {
        brew_str("-");
    }
    brew_str("\n");
}

// Main cowsay function
static void cowsay(const char* message) {
    int len = brew_strlen(message);
    
    // Draw speech bubble
    brew_str("\n");  // Start with a newline for better spacing
    draw_top_border(len);
    brew_str("< ");
    brew_str(message);
    brew_str(" >\n");
    draw_bottom_border(len);

    // Draw the cow
    brew_str("        \\   ^__^\n");
    brew_str("         \\  (oo)\\_______\n");
    brew_str("            (__)\\       )\\/\\\n");
    brew_str("                ||----w |\n");
    brew_str("                ||     ||\n\n");
}

// Entry point for the cowsay command
static void display_cowsay(const char* command) {
    const char* message;
    if (!command || brew_strlen(command) == 0) {
        message = "Brew!";
    } else {
        message = find_args(command);
        if (brew_strlen(message) == 0) {
            message = "Brew!";
        }
    }
    cowsay(message);
}
#endif // APPS_COWSAY_H
