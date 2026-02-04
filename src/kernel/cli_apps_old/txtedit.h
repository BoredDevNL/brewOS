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

#ifndef TXTEDIT_H
#define TXTEDIT_H

#include "../print.h"
#include "../keyboard.h"
#include "../filesys.h"
#include "../file.h"

#define BUFFER_SIZE 4096
#define MAX_LINES 25
#define MAX_LINE_LENGTH 80

// Helper string functions for txtedit
static int strcmp_txtedit(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static size_t strlen_txtedit(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

// Buffer for the current file being edited
static char text_buffer[BUFFER_SIZE];
static int buffer_size = 0;
static int cursor_pos = 0;
static int cursor_row = 0;
static int cursor_col = 0;
static int scroll_offset = 0;  // Line number at top of visible area
static int has_unsaved_changes = 0;  // Flag to track if file has been modified
#define ESC_KEY 0x01
#define ENTER_KEY 0x1C
#define BACKSPACE_KEY 0x0E
#define SCAN_CODE_LEFT_ARROW 0x4B
#define SCAN_CODE_RIGHT_ARROW 0x4D
#define VISIBLE_LINES 23  // Lines 0-22 visible (line 23 is status)

// Status messages
static const char* MSG_SAVED = "Text saved to RAM buffer";
static const char* MSG_LOADED = "Text loaded from RAM buffer";
static const char* MSG_HELP = "ESC:Exit ENTER:NewLine Arrows:Navigate";

static void draw_status_line(const char* msg) {
    size_t row, col;
    print_get_cursor_pos(&row, &col);
    print_set_cursor_pos(24, 0);  // Bottom line
    print_set_color(PRINT_INDEX_0, PRINT_INDEX_7);  // Black on light grey
    for(int i = 0; i < 80; i++) print_char(' ');  // Clear line
    print_set_cursor_pos(24, 0);
    brew_str(msg);
    print_set_cursor_pos(row, col);
    print_set_color(PRINT_INDEX_7, PRINT_INDEX_0);  // Reset colors to latte
}

static void get_filename(char* filename) {
    int pos = 0;
    print_clear();
    brew_str("Enter filename (e.g., file.txt): ");
    
    while(1) {
        if(check_keyboard()) {
            unsigned char scan_code = read_scan_code();
            
            if(scan_code == 0x1C) { // Enter
                filename[pos] = '\0';
                break;
            }
            else if(scan_code == 0x0E) { // Backspace
                if(pos > 0) {
                    pos--;
                    print_backspace();
                }
            }
            else {
                char c = scan_code_to_ascii(scan_code);
                if(c && pos < FS_MAX_FILENAME - 1) {
                    filename[pos++] = c;
                    print_char(c);
                }
            }
        }
    }
}

static char current_filename[FS_MAX_FILENAME];

static void save_current_buffer(const char* text_buffer, int buffer_size, bool prompt_for_filename) {
    // If we don't have a current filename and need to prompt, ask for one
    if (current_filename[0] == '\0' && prompt_for_filename) {
        get_filename(current_filename);
    } else if (current_filename[0] == '\0') {
        draw_status_line("Error: No filename specified");
        return;
    }
    
    // Create a new file in the current directory
    File* file = fs_find_file(current_filename);
    if (!file) {
        file = fs_create_file(current_filename);
    }
    
    if (file && file_write_content(file, text_buffer, buffer_size)) {
        draw_status_line("File saved successfully");
    } else {
        draw_status_line("Error: Could not save file");
    }
}

static void calculate_cursor_position() {
    // Calculate cursor position based on buffer position
    cursor_row = 0;
    cursor_col = 0;
    int current_line = 0;
    
    for(int i = 0; i < cursor_pos && i < buffer_size; i++) {
        if(text_buffer[i] == '\n') {
            current_line++;
            cursor_col = 0;
        } else {
            cursor_col++;
        }
    }
    
    // Adjust display row based on scroll offset
    cursor_row = current_line - scroll_offset;
    
    // Auto-scroll up if cursor goes above visible area
    if(cursor_row < 0) {
        scroll_offset += cursor_row;
        cursor_row = 0;
    }
    
    // Auto-scroll down if cursor goes below visible area
    if(cursor_row >= VISIBLE_LINES) {
        scroll_offset += (cursor_row - VISIBLE_LINES + 1);
        cursor_row = VISIBLE_LINES - 1;
    }
}

static void redraw_screen() {
    print_disable_cursor();
    
    print_clear();
    print_set_cursor_pos(0, 0);
    
    // Draw the text buffer, starting from scroll_offset
    int current_line = 0;
    int display_line = 0;
    
    for(int i = 0; i < buffer_size && display_line < VISIBLE_LINES; i++) {
        // Skip lines until we reach scroll_offset
        if(current_line < scroll_offset) {
            if(text_buffer[i] == '\n') {
                current_line++;
            }
            continue;
        }
        
        // We're now in the visible area
        if(text_buffer[i] == '\n') {
            print_char('\n');
            current_line++;
            display_line++;
            if(display_line >= VISIBLE_LINES) break;
        } else {
            print_char(text_buffer[i]);
        }
    }
    
    // Calculate cursor position based on cursor_pos in buffer
    calculate_cursor_position();
    
    draw_status_line(MSG_HELP);
    // Position cursor correctly and re-enable it
    print_set_cursor_pos(cursor_row, cursor_col);
    print_enable_cursor();
}

static void insert_char(char c) {
    if(buffer_size >= BUFFER_SIZE - 1) return;
    
    // Move following text forward
    for(int i = buffer_size; i > cursor_pos; i--) {
        text_buffer[i] = text_buffer[i-1];
    }
    
    text_buffer[cursor_pos] = c;
    cursor_pos++;
    buffer_size++;
    has_unsaved_changes = 1;  // Mark file as modified
    
    // Redraw the screen to properly display the inserted character
    redraw_screen();
}

static void delete_char() {
    if(cursor_pos <= 0) return;
    
    cursor_pos--;
    // Move following text backward
    for(int i = cursor_pos; i < buffer_size - 1; i++) {
        text_buffer[i] = text_buffer[i+1];
    }
    buffer_size--;
    has_unsaved_changes = 1;  // Mark file as modified
    
    // Redraw the screen to properly display after deletion
    redraw_screen();
}

static void handle_special_key(unsigned char scan_code) {
    switch(scan_code) {
        case SCAN_CODE_UP_ARROW: {
            // Find the start of the current line
            int line_start = cursor_pos;
            while(line_start > 0 && text_buffer[line_start - 1] != '\n') {
                line_start--;
            }
            
            // If we're on the first line, can't go up
            if(line_start == 0) break;
            
            // Find the start of the previous line
            int prev_line_start = line_start - 1;
            while(prev_line_start > 0 && text_buffer[prev_line_start - 1] != '\n') {
                prev_line_start--;
            }
            
            // Calculate target column position
            int target_col = cursor_pos - line_start;
            
            // Find the end of the previous line
            int prev_line_end = prev_line_start;
            while(prev_line_end < line_start - 1 && text_buffer[prev_line_end] != '\n') {
                prev_line_end++;
            }
            
            // Move cursor to same column in previous line (or end of line if shorter)
            int prev_line_length = prev_line_end - prev_line_start;
            cursor_pos = prev_line_start + (target_col < prev_line_length ? target_col : prev_line_length);
            calculate_cursor_position();
            redraw_screen();
            break;
        }
            
        case SCAN_CODE_DOWN_ARROW: {
            // Find the start of the current line
            int line_start = cursor_pos;
            while(line_start > 0 && text_buffer[line_start - 1] != '\n') {
                line_start--;
            }
            
            // Find the end of the current line
            int line_end = cursor_pos;
            while(line_end < buffer_size && text_buffer[line_end] != '\n') {
                line_end++;
            }
            
            // If we're on the last line, can't go down
            if(line_end >= buffer_size) break;
            
            // Move to start of next line
            int next_line_start = line_end + 1;
            if(next_line_start >= buffer_size) break;
            
            // Find the end of the next line
            int next_line_end = next_line_start;
            while(next_line_end < buffer_size && text_buffer[next_line_end] != '\n') {
                next_line_end++;
            }
            
            // Calculate target column position
            int target_col = cursor_pos - line_start;
            
            // Move cursor to same column in next line (or end of line if shorter)
            int next_line_length = next_line_end - next_line_start;
            cursor_pos = next_line_start + (target_col < next_line_length ? target_col : next_line_length);
            calculate_cursor_position();
            redraw_screen();
            break;
        }
            
        case SCAN_CODE_LEFT_ARROW:
            if(cursor_pos > 0) {
                cursor_pos--;
                calculate_cursor_position();
                redraw_screen();
            }
            break;
            
        case SCAN_CODE_RIGHT_ARROW:
            if(cursor_pos < buffer_size) {
                cursor_pos++;
                calculate_cursor_position();
                redraw_screen();
            }
            break;
    }
}

static void load_file(const char* filename) {
    File* file = fs_find_file(filename);
    if (!file) {
        draw_status_line("Creating new file");
        return;
    }

    size_t size;
    const char* content = file_get_content(file, &size);
    if (!content || size > BUFFER_SIZE) {
        draw_status_line("Error: Could not load file or file too large");
        return;
    }

    // Copy content to buffer
    for (size_t i = 0; i < size; i++) {
        text_buffer[i] = content[i];
    }
    buffer_size = size;
    cursor_pos = 0;  // Start at beginning of file
    has_unsaved_changes = 0;  // File is not modified after loading
    draw_status_line("File loaded successfully");
}

void txtedit_run(const char* filename) {
    print_set_color(PRINT_INDEX_7, PRINT_INDEX_0);  // Set initial color to latte
    
    // Clear the buffers and filename
    for(int i = 0; i < BUFFER_SIZE; i++) {
        text_buffer[i] = 0;
    }
    buffer_size = 0;
    cursor_pos = 0;
    scroll_offset = 0;
    has_unsaved_changes = 0;
    current_filename[0] = '\0';

    // If filename is provided, store it
    if (filename) {
        int i;
        for (i = 0; filename[i] && i < FS_MAX_FILENAME - 1; i++) {
            current_filename[i] = filename[i];
        }
        current_filename[i] = '\0';
        load_file(filename);
    }
    
    print_clear();
    redraw_screen();
    print_enable_cursor();
    
    while(1) {
        if(check_keyboard()) {
            unsigned char scan_code = read_scan_code();
            
            if(scan_code == ESC_KEY) {
                // Only ask to save if there are unsaved changes
                if(has_unsaved_changes) {
                    draw_status_line("Save before exit? (Y/N)");
                    while(1) {
                        if(check_keyboard()) {
                            unsigned char save_scan = read_scan_code();
                            char save_char = scan_code_to_ascii(save_scan);
                            if(save_char == 'Y' || save_char == 'y') {
                                // Only prompt for filename if we don't have one (started with txtedit with no args)
                                save_current_buffer(text_buffer, buffer_size, current_filename[0] == '\0');
                                break;
                            }
                            else if(save_char == 'N' || save_char == 'n') {
                                break;
                            }
                        }
                    }
                }
                break;
            }
            else if(scan_code == ENTER_KEY) {
                insert_char('\n');
            }
            else if(scan_code == BACKSPACE_KEY) {
                delete_char();
            }
            else if(scan_code == SCAN_CODE_UP_ARROW || scan_code == SCAN_CODE_DOWN_ARROW || 
                    scan_code == SCAN_CODE_LEFT_ARROW || scan_code == SCAN_CODE_RIGHT_ARROW) {
                handle_special_key(scan_code);
            }
            else {
                char c = scan_code_to_ascii(scan_code);
                if(c) insert_char(c);
            }
        }
    }
    

}

#endif // TXTEDIT_H