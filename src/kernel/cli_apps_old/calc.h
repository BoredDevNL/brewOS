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
#ifndef APPS_CALC_H
#define APPS_CALC_H

#include "print.h"
#include "keyboard.h"

extern int buffer_pos;

// Calculator state structure
struct {
    char display[64];      // Display buffer for calculation
    int display_len;
    long current_val;
    long prev_val;
    char operation;
    int cursor_row;        // 0-3 for numpad rows
    int cursor_col;        // 0-3 for numpad cols
    int has_operation;
} calc_state;

// Redraw the calculator UI
static void calc_redraw_ui() {
    // Clear screen by printing newlines (but fewer to avoid deadspace)
    for (int i = 0; i < 24; i++) {
        brew_str("\n");
    }
    
    // Display header
    brew_str("===== CALCULATOR =====\n");
    brew_str("Display: ");
    brew_str(calc_state.display);
    brew_str("\n\n");
    
    // Draw numpad with cursor
    const char* buttons[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"0", ".", "=", "+"}
    };
    
    for (int row = 0; row < 4; row++) {
        brew_str("  ");
        for (int col = 0; col < 4; col++) {
            if (row == calc_state.cursor_row && col == calc_state.cursor_col) {
                brew_str("[");
                brew_str(buttons[row][col]);
                brew_str("]");
            } else {
                brew_str(" ");
                brew_str(buttons[row][col]);
                brew_str(" ");
            }
            brew_str("  ");
        }
        brew_str("\n");
    }
    
    brew_str("\nNavigate: Arrow Keys | Select: Enter | Clear: C | Quit: Q/ESC\n");
}

// Get button at current cursor position
static const char* calc_get_button() {
    const char* buttons[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"0", ".", "=", "+"}
    };
    return buttons[calc_state.cursor_row][calc_state.cursor_col];
}

// Append character to display
static void calc_append_to_display(const char* text) {
    int i = 0;
    while (text[i] && calc_state.display_len < 63) {
        calc_state.display[calc_state.display_len++] = text[i++];
    }
    calc_state.display[calc_state.display_len] = '\0';
}

// Parse and calculate result
static void calc_perform_calculation() {
    if (calc_state.display_len == 0) return;
    
    // Parse the current display value
    long val = 0;
    int i = 0;
    int is_negative = 0;
    
    if (calc_state.display[0] == '-') {
        is_negative = 1;
        i = 1;
    }
    
    while (i < calc_state.display_len && calc_state.display[i] != '.') {
        if (calc_state.display[i] >= '0' && calc_state.display[i] <= '9') {
            val = val * 10 + (calc_state.display[i] - '0');
        }
        i++;
    }
    
    if (is_negative) val = -val;
    
    long result = val;
    
    if (calc_state.has_operation) {
        switch (calc_state.operation) {
            case '+': result = calc_state.prev_val + val; break;
            case '-': result = calc_state.prev_val - val; break;
            case '*': result = calc_state.prev_val * val; break;
            case '/': 
                if (val != 0) result = calc_state.prev_val / val;
                else {
                    calc_state.display_len = 0;
                    calc_append_to_display("ERROR");
                    return;
                }
                break;
        }
    }
    
    // Convert result back to string
    calc_state.display_len = 0;
    if (result < 0) {
        calc_append_to_display("-");
        result = -result;
    }
    
    if (result == 0) {
        calc_append_to_display("0");
    } else {
        char temp[32];
        int len = 0;
        long temp_val = result;
        while (temp_val > 0) {
            temp[len++] = '0' + (temp_val % 10);
            temp_val /= 10;
        }
        
        // Reverse and append
        for (int j = len - 1; j >= 0; j--) {
            char c[2] = {temp[j], '\0'};
            calc_append_to_display(c);
        }
    }
    
    calc_state.prev_val = result;
}

// Main calculator command
static void calc_cmd() {
    // Initialize calculator state
    calc_state.display[0] = '\0';
    calc_state.display_len = 0;
    calc_state.current_val = 0;
    calc_state.prev_val = 0;
    calc_state.operation = '\0';
    calc_state.cursor_row = 0;
    calc_state.cursor_col = 0;
    calc_state.has_operation = 0;
    
    calc_redraw_ui();
    
    buffer_pos = 0;
    
    while (1) {
        if (check_keyboard()) {
            unsigned char scan_code = read_scan_code();
            
            // Handle arrow keys for navigation
            if (scan_code == 0x48) { // UP arrow
                if (calc_state.cursor_row > 0) {
                    calc_state.cursor_row--;
                    calc_redraw_ui();
                }
                continue;
            } else if (scan_code == 0x50) { // DOWN arrow
                if (calc_state.cursor_row < 3) {
                    calc_state.cursor_row++;
                    calc_redraw_ui();
                }
                continue;
            } else if (scan_code == 0x4B) { // LEFT arrow
                if (calc_state.cursor_col > 0) {
                    calc_state.cursor_col--;
                    calc_redraw_ui();
                }
                continue;
            } else if (scan_code == 0x4D) { // RIGHT arrow
                if (calc_state.cursor_col < 3) {
                    calc_state.cursor_col++;
                    calc_redraw_ui();
                }
                continue;
            }
            
            char ascii_char = scan_code_to_ascii(scan_code);
            
            // Handle C key to clear
            if (ascii_char == 'c' || ascii_char == 'C') {
                calc_state.display[0] = '\0';
                calc_state.display_len = 0;
                calc_state.current_val = 0;
                calc_state.prev_val = 0;
                calc_state.operation = '\0';
                calc_state.has_operation = 0;
                calc_redraw_ui();
                continue;
            }
            
            // Handle Q key or ESC to quit
            if (ascii_char == 'q' || ascii_char == 'Q' || scan_code == 0x01) {
                brew_str("\n");
                return;
            }
            
            // Handle Enter to select button
            if (ascii_char == '\n' || scan_code == 0x1C) {
                const char* button = calc_get_button();
                
                if (button[0] >= '0' && button[0] <= '9') {
                    // Number button
                    calc_append_to_display(button);
                    calc_redraw_ui();
                } else if (button[0] == '.') {
                    // Decimal point
                    int has_dot = 0;
                    for (int i = 0; i < calc_state.display_len; i++) {
                        if (calc_state.display[i] == '.') {
                            has_dot = 1;
                            break;
                        }
                    }
                    if (!has_dot && calc_state.display_len > 0) {
                        calc_append_to_display(".");
                        calc_redraw_ui();
                    }
                } else if (button[0] == '=' || button[0] == '+' || 
                          button[0] == '-' || button[0] == '*' || button[0] == '/') {
                    
                    if (button[0] == '=') {
                        if (calc_state.has_operation && calc_state.display_len > 0) {
                            calc_perform_calculation();
                            calc_state.operation = '\0';
                            calc_state.has_operation = 0;
                            calc_redraw_ui();
                        }
                    } else {
                        // Operation button pressed
                        if (calc_state.display_len > 0) {
                            // If we already have an operation pending, calculate first
                            if (calc_state.has_operation) {
                                calc_perform_calculation();
                            } else {
                                // First operation: save display value to prev_val
                                calc_perform_calculation();
                            }
                            // Set new operation
                            calc_state.operation = button[0];
                            calc_state.has_operation = 1;
                            calc_state.display_len = 0;
                            calc_state.display[0] = '\0';
                            calc_redraw_ui();
                        }
                    }
                    continue;
                }
            }
        }
    }
}

#endif // APPS_CALC_H