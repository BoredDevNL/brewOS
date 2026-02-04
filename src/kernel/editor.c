#include "editor.h"
#include "graphics.h"
#include "fat32.h"
#include "wm.h"
#include <stdbool.h>
#include <stddef.h>

// === Text Editor State ===
Window win_editor;

#define EDITOR_MAX_LINES 128
#define EDITOR_MAX_LINE_LEN 256
#define EDITOR_LINE_HEIGHT 16
#define EDITOR_CHAR_WIDTH 8

typedef struct {
    char content[EDITOR_MAX_LINE_LEN];
    int length;
} EditorLine;

static EditorLine lines[EDITOR_MAX_LINES];
static int line_count = 1;
static int cursor_line = 0;
static int cursor_col = 0;
static int scroll_top = 0;
static char open_filename[256] = "";
static bool file_modified = false;

// === Helper Functions ===

static size_t editor_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static void editor_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int editor_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// === Editor Logic ===

// Forward declaration
static void editor_ensure_cursor_visible(void);

static void editor_clear_all(void) {
    for (int i = 0; i < EDITOR_MAX_LINES; i++) {
        lines[i].content[0] = 0;
        lines[i].length = 0;
    }
    line_count = 1;
    cursor_line = 0;
    cursor_col = 0;
    scroll_top = 0;
    open_filename[0] = 0;
    file_modified = false;
}

void editor_open_file(const char *filename) {
    editor_clear_all();
    editor_strcpy(open_filename, filename);
    
    FAT32_FileHandle *fh = fat32_open(filename, "r");
    if (!fh) {
        // New file
        file_modified = false;
        return;
    }
    
    // Read file content
    char buffer[16384];
    int bytes_read = fat32_read(fh, buffer, sizeof(buffer));
    fat32_close(fh);
    
    if (bytes_read <= 0) {
        file_modified = false;
        return;
    }
    
    // Parse into lines
    int line = 0;
    int col = 0;
    
    for (int i = 0; i < bytes_read && line < EDITOR_MAX_LINES; i++) {
        char ch = buffer[i];
        
        if (ch == '\n') {
            lines[line].content[col] = 0;
            lines[line].length = col;
            line++;
            col = 0;
        } else if (ch != '\r') {
            if (col < EDITOR_MAX_LINE_LEN - 1) {
                lines[line].content[col] = ch;
                col++;
            }
        }
    }
    
    if (col > 0) {
        lines[line].content[col] = 0;
        lines[line].length = col;
        line++;
    }
    
    line_count = (line > 0) ? line : 1;
    file_modified = false;
}

static void editor_save_file(void) {
    if (!open_filename[0]) {
        // No filename set
        return;
    }
    
    FAT32_FileHandle *fh = fat32_open(open_filename, "w");
    if (!fh) {
        return;
    }
    
    // Write lines
    for (int i = 0; i < line_count; i++) {
        fat32_write(fh, lines[i].content, lines[i].length);
        fat32_write(fh, "\n", 1);
    }
    
    fat32_close(fh);
    file_modified = false;
}

// Insert character at cursor position
static void editor_insert_char(char ch) {
    if (cursor_line >= EDITOR_MAX_LINES) return;
    
    EditorLine *line = &lines[cursor_line];
    
    if (ch == '\n') {
        // Split line - shift all lines below down first
        if (line_count >= EDITOR_MAX_LINES) return;
        
        // Shift all lines from cursor_line+1 onwards down by one position
        for (int j = line_count; j > cursor_line; j--) {
            lines[j] = lines[j - 1];
        }
        line_count++;
        
        // Clear the new line completely (zero entire buffer)
        for (int k = 0; k < EDITOR_MAX_LINE_LEN; k++) {
            lines[cursor_line + 1].content[k] = 0;
        }
        lines[cursor_line + 1].length = 0;
        
        // Now split the current line at cursor position
        int current_len = lines[cursor_line].length;
        int new_len = current_len - cursor_col;
        
        // Copy the second part to the new line
        for (int i = 0; i < new_len; i++) {
            lines[cursor_line + 1].content[i] = lines[cursor_line].content[cursor_col + i];
        }
        lines[cursor_line + 1].content[new_len] = 0;
        lines[cursor_line + 1].length = new_len;
        
        // Truncate current line
        lines[cursor_line].content[cursor_col] = 0;
        lines[cursor_line].length = cursor_col;
        
        cursor_line++;
        cursor_col = 0;
    } else if (ch == '\b') {
        // Backspace
        if (cursor_col > 0) {
            for (int i = cursor_col - 1; i < line->length; i++) {
                line->content[i] = line->content[i + 1];
            }
            line->length--;
            cursor_col--;
        } else if (cursor_line > 0) {
            // Merge with previous line
            EditorLine *prev = &lines[cursor_line - 1];
            int merge_point = prev->length;
            
            for (int i = 0; i < line->length; i++) {
                if (merge_point + i < EDITOR_MAX_LINE_LEN - 1) {
                    prev->content[merge_point + i] = line->content[i];
                }
            }
            prev->content[merge_point + line->length] = 0;
            prev->length = merge_point + line->length;
            
            // Shift lines up
            for (int i = cursor_line; i < line_count - 1; i++) {
                lines[i] = lines[i + 1];
            }
            lines[line_count - 1].length = 0;
            lines[line_count - 1].content[0] = 0;
            
            cursor_line--;
            cursor_col = merge_point;
            line_count--;
        }
    } else if (ch >= 32 && ch <= 126) {
        // Regular character
        if (cursor_col < EDITOR_MAX_LINE_LEN - 1) {
            // Shift characters right
            for (int i = line->length; i > cursor_col; i--) {
                line->content[i] = line->content[i - 1];
            }
            line->content[cursor_col] = ch;
            line->length++;
            cursor_col++;
        }
    }
    
    file_modified = true;
    editor_ensure_cursor_visible();
}

// Ensure cursor is visible by adjusting scroll position
static void editor_ensure_cursor_visible(void) {
    int visible_lines = 22;  // Allow ~24 lines to use available window space
    
    // Scroll up if cursor is above visible area
    if (cursor_line < scroll_top) {
        scroll_top = cursor_line;
    }
    
    // Scroll down if cursor is below visible area
    if (cursor_line >= scroll_top + visible_lines) {
        scroll_top = cursor_line - visible_lines + 1;
    }
}

// === Paint Function ===

static void editor_paint(Window *win) {
    int offset_x = win->x + 4;
    int offset_y = win->y + 24;
    int content_width = win->w - 8;
    int content_height = win->h - 28;
    
    // Draw filename and save button area at top of content
    draw_rect(offset_x, offset_y, content_width, 25, COLOR_GRAY);
    draw_string(offset_x + 10, offset_y + 5, "File: ", COLOR_BLACK);
    draw_string(offset_x + 55, offset_y + 5, open_filename, COLOR_BLACK);
    
    // Draw save button
    draw_button(offset_x + content_width - 80, offset_y + 3, 70, 20, "Save", false);
    
    // Draw modification indicator
    if (file_modified) {
        draw_string(offset_x + content_width - 200, offset_y + 5, "[Modified]", COLOR_RED);
    }
    
    // Fill editor background
    draw_rect(offset_x, offset_y + 30, content_width, content_height - 55, COLOR_WHITE);
    
    // Draw line numbers and content
    int visible_lines = (content_height - 55) / EDITOR_LINE_HEIGHT;
    int max_line = scroll_top + visible_lines;
    if (max_line > line_count) max_line = line_count;
    
    for (int i = scroll_top; i < max_line; i++) {
        int display_y = offset_y + 35 + (i - scroll_top) * EDITOR_LINE_HEIGHT;
        
        // Draw line number
        char line_num_str[16];
        int temp = i + 1;
        int str_len = 0;
        if (temp == 0) {
            line_num_str[0] = '0';
            str_len = 1;
        } else {
            while (temp > 0) {
                line_num_str[str_len++] = (temp % 10) + '0';
                temp /= 10;
            }
            // Reverse
            for (int j = 0; j < str_len / 2; j++) {
                char t = line_num_str[j];
                line_num_str[j] = line_num_str[str_len - 1 - j];
                line_num_str[str_len - 1 - j] = t;
            }
        }
        line_num_str[str_len] = 0;
        draw_string(offset_x + 4, display_y, line_num_str, COLOR_DKGRAY);
        
        // Draw line content
        draw_string(offset_x + 40, display_y, lines[i].content, COLOR_BLACK);
        
        // Draw cursor if on this line
        if (i == cursor_line) {
            int cursor_x = offset_x + 40 + (cursor_col * EDITOR_CHAR_WIDTH);
            draw_rect(cursor_x, display_y, 2, 10, COLOR_BLACK);
        }
    }
    
    // Draw status bar at bottom
    draw_rect(offset_x, offset_y + content_height - 20, content_width, 20, COLOR_GRAY);
    draw_string(offset_x + 10, offset_y + content_height - 15, "Line: ", COLOR_WHITE);
    
    char line_str[32];
    int temp = cursor_line + 1;
    int idx = 0;
    while (temp > 0) {
        line_str[idx++] = (temp % 10) + '0';
        temp /= 10;
    }
    for (int j = 0; j < idx / 2; j++) {
        char t = line_str[j];
        line_str[j] = line_str[idx - 1 - j];
        line_str[idx - 1 - j] = t;
    }
    line_str[idx] = 0;
    
    draw_string(offset_x + 60, offset_y + content_height - 15, line_str, COLOR_WHITE);
    draw_string(offset_x + 100, offset_y + content_height - 15, "  Col: ", COLOR_WHITE);
    
    char col_str[32];
    temp = cursor_col + 1;
    idx = 0;
    while (temp > 0) {
        col_str[idx++] = (temp % 10) + '0';
        temp /= 10;
    }
    for (int j = 0; j < idx / 2; j++) {
        char t = col_str[j];
        col_str[j] = col_str[idx - 1 - j];
        col_str[idx - 1 - j] = t;
    }
    col_str[idx] = 0;
    
    draw_string(offset_x + 170, offset_y + content_height - 15, col_str, COLOR_WHITE);
}

// === Key Handler ===

static void editor_handle_key(Window *win, char c) {
    if (c == 'q' || c == 'Q') {
        if (file_modified) {
        }
        win->visible = false;
        return;
    }
    
    // Arrow keys - UP
    if (c == 17) {
        if (cursor_line > 0) {
            cursor_line--;
            if (cursor_col > (int)lines[cursor_line].length) {
                cursor_col = lines[cursor_line].length;
            }
            if (cursor_line < scroll_top) {
                scroll_top = cursor_line;
            }
        }
        return;
    }
    
    // Arrow keys - DOWN
    if (c == 18) {
        if (cursor_line < line_count - 1) {
            cursor_line++;
            if (cursor_col > (int)lines[cursor_line].length) {
                cursor_col = lines[cursor_line].length;
            }
            int visible_lines = 20;
            if (cursor_line >= scroll_top + visible_lines) {
                scroll_top = cursor_line - visible_lines + 1;
            }
        }
        return;
    }
    
    // Arrow keys - LEFT
    if (c == 19) {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_line > 0) {
            cursor_line--;
            cursor_col = lines[cursor_line].length;
        }
        return;
    }
    
    // Arrow keys - RIGHT
    if (c == 20) {
        if (cursor_col < (int)lines[cursor_line].length) {
            cursor_col++;
        } else if (cursor_line < line_count - 1) {
            cursor_line++;
            cursor_col = 0;
        }
        return;
    }
    
    // Regular character input
    editor_insert_char(c);
}

// === Click Handler ===

static void editor_handle_click(Window *win, int x, int y) {
    // x and y are relative to window origin
    int content_width = win->w - 8;
    
    // Check save button - position is at (4 + content_width - 80, 24 + 3) = (4 + w - 8 - 80, 27)
    // Button dimensions: 70 wide, 20 tall
    int button_x = 4 + content_width - 80;
    int button_y = 24 + 3;
    
    if (x >= button_x && x < button_x + 70 &&
        y >= button_y && y < button_y + 20) {
        editor_save_file();
        return;
    }
}

// === Initialization ===

void editor_init(void) {
    win_editor.title = "Text Editor";
    win_editor.x = 100;
    win_editor.y = 150;
    win_editor.w = 700;
    win_editor.h = 450;
    win_editor.visible = false;
    win_editor.focused = false;
    win_editor.z_index = 0;
    win_editor.paint = editor_paint;
    win_editor.handle_key = editor_handle_key;
    win_editor.handle_click = editor_handle_click;
    win_editor.handle_right_click = NULL;
    
    editor_clear_all();
}
