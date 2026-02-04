#include "calculator.h"
#include "graphics.h"
#include "wm.h"
#include <stdbool.h>
#include <stddef.h>

Window win_calculator;

static long long calc_acc = 0;
static long long calc_curr = 0;
static char calc_op = 0;
static bool calc_new_entry = true;
static bool calc_error = false;

// Helper to convert int to string
static void int_to_str(long long n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    
    int i = 0;
    bool neg = n < 0;
    if (neg) n = -n;
    
    char temp[32];
    while (n > 0) {
        temp[i++] = '0' + (n % 10);
        n /= 10;
    }
    if (neg) temp[i++] = '-';
    
    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = 0;
}

static void update_display(Window *win) {
    if (calc_error) {
        char *err = "Error";
        int i = 0; while(err[i]) { win->buffer[i] = err[i]; i++; }
        win->buffer[i] = 0;
    } else {
        int_to_str(calc_curr, win->buffer);
    }
    win->buf_len = 0; while(win->buffer[win->buf_len]) win->buf_len++;
}

static void calculator_paint(Window *win) {
    // Background
    draw_rect(win->x + 4, win->y + 24, win->w - 8, win->h - 28, COLOR_LTGRAY);
    
    // Display Area
    draw_bevel_rect(win->x + 10, win->y + 30, win->w - 20, 25, true);
    // Right align text
    int text_w = win->buf_len * 8;
    int text_x = win->x + win->w - 15 - text_w;
    draw_string(text_x, win->y + 38, win->buffer, COLOR_BLACK);
    
    // Buttons
    const char *labels[] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", "C", "=", "+"
    };
    
    int bw = 30;
    int bh = 25;
    int gap = 5;
    int start_x = win->x + 10;
    int start_y = win->y + 65;
    
    for (int i = 0; i < 16; i++) {
        int r = i / 4;
        int c = i % 4;
        draw_button(start_x + c*(bw+gap), start_y + r*(bh+gap), bw, bh, labels[i], false);
    }
}

static void do_op(void) {
    if (calc_op == '+') calc_acc += calc_curr;
    else if (calc_op == '-') calc_acc -= calc_curr;
    else if (calc_op == '*') calc_acc *= calc_curr;
    else if (calc_op == '/') {
        if (calc_curr == 0) calc_error = true;
        else calc_acc /= calc_curr;
    } else {
        calc_acc = calc_curr;
    }
}

static void calculator_click(Window *win, int x, int y) {
    int bw = 30;
    int bh = 25;
    int gap = 5;
    int start_x = 10;
    int start_y = 65;
    
    for (int i = 0; i < 16; i++) {
        int r = i / 4;
        int c = i % 4;
        int bx = start_x + c*(bw+gap);
        int by = start_y + r*(bh+gap);
        
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            // Clicked button i
            const char *labels[] = {
                "7", "8", "9", "/",
                "4", "5", "6", "*",
                "1", "2", "3", "-",
                "0", "C", "=", "+"
            };
            char lbl = labels[i][0];
            
            if (lbl >= '0' && lbl <= '9') {
                if (calc_new_entry || calc_curr == 0) {
                    calc_curr = lbl - '0';
                    calc_new_entry = false;
                } else {
                    calc_curr = calc_curr * 10 + (lbl - '0');
                }
                calc_error = false;
            } else if (lbl == 'C') {
                calc_curr = 0;
                calc_acc = 0;
                calc_op = 0;
                calc_new_entry = true;
                calc_error = false;
            } else if (lbl == '=') {
                do_op();
                calc_curr = calc_acc;
                calc_op = 0;
                calc_new_entry = true;
            } else {
                if (!calc_new_entry) {
                    if (calc_op) do_op();
                    else calc_acc = calc_curr;
                }
                calc_op = lbl;
                calc_new_entry = true;
            }
            
            update_display(win);
            wm_paint(); // Request repaint
            return;
        }
    }
}

void calculator_init(void) {
    win_calculator.title = "Calculator";
    win_calculator.x = 200;
    win_calculator.y = 200;
    win_calculator.w = 160;
    win_calculator.h = 200;
    win_calculator.visible = false;
    win_calculator.focused = false;
    win_calculator.z_index = 0;
    win_calculator.paint = calculator_paint;
    win_calculator.handle_click = calculator_click;
    win_calculator.handle_right_click = NULL;
    
    calc_curr = 0;
    calc_acc = 0;
    calc_op = 0;
    calc_new_entry = true;
    update_display(&win_calculator);
}