#include "notepad.h"
#include "graphics.h"
#include <stddef.h>

Window win_notepad;
static int notepad_scroll_line = 0;  

static void notepad_ensure_cursor_visible(Window *win) {
    int visible_lines = (win->h - 40) / 10 + 3; 
    if (visible_lines < 1) visible_lines = 1;
    
    int cursor_line = 0;
    for (int i = 0; i < win->cursor_pos && i < win->buf_len; i++) {
        if (win->buffer[i] == '\n') cursor_line++;
    }
    
    if (cursor_line < notepad_scroll_line) {
        notepad_scroll_line = cursor_line;
    }
    
    if (cursor_line >= notepad_scroll_line + visible_lines) {
        notepad_scroll_line = cursor_line - visible_lines + 1;
    }
}

static void notepad_paint(Window *win) {
    int visual_line = 0; 
    int current_x = win->x + 8;
    int current_y = win->y + 30;
    int window_right = win->x + win->w - 16;  
    
    for (int i = 0; i < win->buf_len; i++) {
        if (visual_line < notepad_scroll_line) {
            if (win->buffer[i] == '\n') {
                visual_line++;
                current_x = win->x + 8;
                current_y = win->y + 30;
            } else {
                if (current_x >= window_right) {
                    visual_line++;
                    current_x = win->x + 8;
                    current_y += 10;
                }
                current_x += 8;
            }
            continue;
        }
        
        if (visual_line >= notepad_scroll_line + (win->h - 40) / 10) {
            break;
        }
        
        if (win->buffer[i] == '\n') {
            current_x = win->x + 8;
            current_y += 10;
            visual_line++;
        } else {
            if (current_x >= window_right) {
                current_x = win->x + 8;
                current_y += 10;
                visual_line++;
                
                if (visual_line >= notepad_scroll_line + (win->h - 40) / 10) {
                    break;
                }
            }
            
            char ch[2] = {win->buffer[i], 0};
            draw_string(current_x, current_y, ch, COLOR_BLACK);
            current_x += 8;
        }
    }
    
    // Cursor
    if (win->focused) {
        int cx = win->x + 8;
        int cy = win->y + 30;
        int visual_line = 0;
        int window_right = win->x + win->w - 16;  // Right boundary with padding
        
        for (int i = 0; i < win->cursor_pos; i++) {
            if (win->buffer[i] == '\n') {
                cx = win->x + 8;
                cy += 10;
                visual_line++;
            } else {
                if (cx >= window_right) {
                    cx = win->x + 8;
                    cy += 10;
                    visual_line++;
                }
                cx += 8;
            }
        }
        
        if (visual_line >= notepad_scroll_line && 
            visual_line < notepad_scroll_line + (win->h - 40) / 10) {
            draw_rect(cx, cy, 2, 8, COLOR_BLACK);
        }
    }
}

static void notepad_key(Window *target, char c) {
    if (c == 17) { // UP
        if (target->cursor_pos > 0) {
            int curr = target->cursor_pos;
            int line_start = curr;
            while (line_start > 0 && target->buffer[line_start - 1] != '\n') {
                line_start--;
            }
            int col = curr - line_start;

            if (line_start > 0) {
                int prev_line_end = line_start - 1;
                int prev_line_start = prev_line_end;
                while (prev_line_start > 0 && target->buffer[prev_line_start - 1] != '\n') {
                    prev_line_start--;
                }
                int prev_line_len = prev_line_end - prev_line_start;
                if (col > prev_line_len) col = prev_line_len;
                target->cursor_pos = prev_line_start + col;
            }
        }
        notepad_ensure_cursor_visible(target);
    } else if (c == 18) { // DOWN
        int len = target->buf_len;
        if (target->cursor_pos < len) {
            int curr = target->cursor_pos;
            int line_start = curr;
            while (line_start > 0 && target->buffer[line_start - 1] != '\n') {
                line_start--;
            }
            int col = curr - line_start;
            
            int next_line_start = curr;
            while (next_line_start < len && target->buffer[next_line_start] != '\n') {
                next_line_start++;
            }
            
            if (next_line_start < len) {
                next_line_start++; // Skip newline
                int next_line_end = next_line_start;
                while (next_line_end < len && target->buffer[next_line_end] != '\n') {
                    next_line_end++;
                }
                int next_line_len = next_line_end - next_line_start;
                if (col > next_line_len) col = next_line_len;
                target->cursor_pos = next_line_start + col;
            } else {
                target->cursor_pos = len;
            }
        }
        notepad_ensure_cursor_visible(target);
    } else if (c == 19) { // LEFT
        if (target->cursor_pos > 0) target->cursor_pos--;
        notepad_ensure_cursor_visible(target);
    } else if (c == 20) { // RIGHT
        if (target->cursor_pos < target->buf_len) target->cursor_pos++;
        notepad_ensure_cursor_visible(target);
    } else if (c == '\b') { // Backspace
        if (target->cursor_pos > 0) {
            // Shift left
            for (int i = target->cursor_pos; i < target->buf_len; i++) {
                target->buffer[i - 1] = target->buffer[i];
            }
            target->buf_len--;
            target->cursor_pos--;
            target->buffer[target->buf_len] = 0;
            notepad_ensure_cursor_visible(target);
        }
    } else if (c == '\n') { // Enter
         if (target->buf_len < 1023) {
             // Shift right
             for (int i = target->buf_len; i > target->cursor_pos; i--) {
                 target->buffer[i] = target->buffer[i - 1];
             }
             target->buffer[target->cursor_pos] = c;
             target->buf_len++;
             target->cursor_pos++;
             target->buffer[target->buf_len] = 0;
             notepad_ensure_cursor_visible(target);
         }
    } else {
        // Printable char
        if (target->buf_len < 1023) {
            for (int i = target->buf_len; i > target->cursor_pos; i--) {
                target->buffer[i] = target->buffer[i - 1];
            }
            target->buffer[target->cursor_pos] = c;
            target->buf_len++;
            target->cursor_pos++;
            target->buffer[target->buf_len] = 0;
            notepad_ensure_cursor_visible(target);
        }
    }
}

void notepad_init(void) {
    win_notepad.title = "Notepad";
    win_notepad.x = 100;
    win_notepad.y = 100;
    win_notepad.w = 400;
    win_notepad.h = 300;
    win_notepad.visible = false;
    win_notepad.buf_len = 0;
    win_notepad.cursor_pos = 0;
    win_notepad.focused = false;
    win_notepad.z_index = 0;
    win_notepad.paint = notepad_paint;
    win_notepad.handle_key = notepad_key;
    win_notepad.handle_click = NULL;
    win_notepad.handle_right_click = NULL;
    
    notepad_scroll_line = 0;
    
    for(int i=0; i<1024; i++) win_notepad.buffer[i] = 0;
}

void notepad_reset(void) {
    // Clear notepad buffer and reset cursor on close/reopen
    win_notepad.buf_len = 0;
    win_notepad.cursor_pos = 0;
    win_notepad.focused = false;
    notepad_scroll_line = 0;
    
    for(int i=0; i<1024; i++) win_notepad.buffer[i] = 0;
}