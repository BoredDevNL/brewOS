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
#ifndef APPS_MATH_H
#define APPS_MATH_H

#include "print.h"
#include "keyboard.h"

extern int buffer_pos;

static void math_cmd() {
        brew_str("\nMath Calculator\n");
        brew_str("Choose operation:\n");
        brew_str("1. Addition (+)\n");
        brew_str("2. Subtraction (-)\n");
        brew_str("3. Multiplication (*)\n");
        brew_str("4. Division (/)\n");
        brew_str("\nEnter operation number: ");
        
        // Clear buffer for new input
        buffer_pos = 0;
        
        // Wait for operation choice
        while (1) {
            if (check_keyboard()) {
                unsigned char scan_code = read_scan_code();
                char ascii_char = scan_code_to_ascii(scan_code);
                
                if (ascii_char >= '1' && ascii_char <= '4') {
                    print_char(ascii_char);
                    char operation = ascii_char;
                    brew_str("\nEnter first number: ");
                    
                    // Get first number
                    buffer_pos = 0;
                    int first_num = 0;
                    while (1) {
                        if (check_keyboard()) {
                            scan_code = read_scan_code();
                            ascii_char = scan_code_to_ascii(scan_code);
                            
                            if (ascii_char >= '0' && ascii_char <= '9') {
                                print_char(ascii_char);
                                first_num = first_num * 10 + (ascii_char - '0');
                            } else if (ascii_char == '\n' || ascii_char == '\r') {
                                break;
                            }
                        }
                    }
                    
                    brew_str("\nEnter second number: ");
                    
                    // Get second number
                    buffer_pos = 0;
                    int second_num = 0;
                    while (1) {
                        if (check_keyboard()) {
                            scan_code = read_scan_code();
                            ascii_char = scan_code_to_ascii(scan_code);
                            
                            if (ascii_char >= '0' && ascii_char <= '9') {
                                print_char(ascii_char);
                                second_num = second_num * 10 + (ascii_char - '0');
                            } else if (ascii_char == '\n' || ascii_char == '\r') {
                                break;
                            }
                        }
                    }
                    
                    brew_str("\nResult: ");
                    switch (operation) {
                        case '1':
                            brew_str("\n");
                            brew_int(first_num);
                            brew_str(" + ");
                            brew_int(second_num);
                            brew_str(" = ");
                            brew_int(first_num + second_num);
                            break;
                        case '2':
                            brew_str("\n");
                            brew_int(first_num);
                            brew_str(" - ");
                            brew_int(second_num);
                            brew_str(" = ");
                            brew_int(first_num - second_num);
                            break;
                        case '3':
                            brew_str("\n");
                            brew_int(first_num);
                            brew_str(" * ");
                            brew_int(second_num);
                            brew_str(" = ");
                            brew_int(first_num * second_num);
                            break;
                        case '4':
                            if (second_num == 0) {
                                brew_str("Error: Division by zero!\n");
                            } else {
                                brew_str("\n");
                                brew_int(first_num);
                                brew_str(" / ");
                                brew_int(second_num);
                                brew_str(" = ");
                                brew_int(first_num / second_num);
                            }
                            break;
                    }
                    brew_str("\n");
                    break;
                }
            }
        }
    }

#endif // APPS_MATH_H