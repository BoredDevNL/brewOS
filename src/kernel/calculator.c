#include "calculator.h"
#include "graphics.h"
#include "wm.h"
#include <stdbool.h>
#include <stddef.h>

Window win_calculator;

#define SCALE 1000000LL

static long long calc_acc = 0;
static long long calc_curr = 0;
static char calc_op = 0;
static bool calc_new_entry = true;
static bool calc_error = false;
static bool calc_decimal_mode = false;
static long long calc_decimal_divisor = 10;

// Simple integer square root
static long long isqrt(long long n) {
    if (n < 0) return -1;
    if (n == 0) return 0;
    long long x = n;
    long long y = 1;
    while (x > y) {
        x = (x + y) / 2;
        y = n / x;
    }
    return x;
}

// Convert fixed point to string
static void fixed_to_str(long long n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    
    char temp[64];
    int pos = 0;
    bool neg = n < 0;
    if (neg) n = -n;
    
    long long int_part = n / SCALE;
    long long frac_part = n % SCALE;
    
    // Fraction part
    char frac_buf[16];
    int f_idx = 0;
    // Fill exactly 6 digits
    for(int k=0; k<6; k++) {
        long long div = 100000;
        for(int m=0; m<k; m++) div /= 10;
        frac_buf[f_idx++] = '0' + ((frac_part / div) % 10);
    }
    frac_buf[f_idx] = 0;
    
    // Trim trailing zeros
    while (f_idx > 0 && frac_buf[f_idx-1] == '0') {
        f_idx--;
    }
    frac_buf[f_idx] = 0;
    
    // If we have a fraction, add it to buffer (reversed first)
    if (f_idx > 0) {
        for (int i = f_idx - 1; i >= 0; i--) {
            temp[pos++] = frac_buf[i];
        }
        temp[pos++] = '.';
    }
    
    // Integer part
    if (int_part == 0) {
        temp[pos++] = '0';
    } else {
        while (int_part > 0) {
            temp[pos++] = '0' + (int_part % 10);
            int_part /= 10;
        }
    }
    
    if (neg) temp[pos++] = '-';
    
    // Reverse into buf
    int j = 0;
    while (pos > 0) {
        buf[j++] = temp[--pos];
    }
    buf[j] = 0;
}

static void update_display(Window *win) {
    if (calc_error) {
        char *err = "Error";
        int i = 0; while(err[i]) { win->buffer[i] = err[i]; i++; }
        win->buffer[i] = 0;
    } else {
        fixed_to_str(calc_curr, win->buffer);
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
    // Layout 4 columns x 5 rows
    const char *labels[] = {
        "C", "sqr", "rt", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", ".", "BS", "="
    };
    
    int bw = 35;
    int bh = 25;
    int gap = 5;
    int start_x = win->x + 10;
    int start_y = win->y + 65;
    
    for (int i = 0; i < 20; i++) {
        int r = i / 4;
        int c = i % 4;
        
        // Special drawing for some buttons?
        draw_button(start_x + c*(bw+gap), start_y + r*(bh+gap), bw, bh, labels[i], false);
    }
}

static void do_op(void) {
    if (calc_op == '+') calc_acc += calc_curr;
    else if (calc_op == '-') calc_acc -= calc_curr;
    else if (calc_op == '*') {
        calc_acc = (calc_acc * calc_curr) / SCALE;
    }
    else if (calc_op == '/') {
        if (calc_curr == 0) calc_error = true;
        else {
            calc_acc = (calc_acc * SCALE) / calc_curr;
        }
    } else {
        calc_acc = calc_curr;
    }
}

static void calculator_click(Window *win, int x, int y) {
    int bw = 35;
    int bh = 25;
    int gap = 5;
    int start_x = 10;
    int start_y = 65;
    
    for (int i = 0; i < 20; i++) {
        int r = i / 4;
        int c = i % 4;
        int bx = start_x + c*(bw+gap);
        int by = start_y + r*(bh+gap);
        
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
             const char *labels[] = {
                "C", "sqr", "rt", "/",
                "7", "8", "9", "*",
                "4", "5", "6", "-",
                "1", "2", "3", "+",
                "0", ".", "BS", "="
            };
            const char *lbl_str = labels[i];
            char lbl = lbl_str[0];
            
            if (lbl >= '0' && lbl <= '9') {
                if (calc_new_entry || calc_error) {
                    calc_curr = (lbl - '0') * SCALE;
                    calc_new_entry = false;
                    calc_decimal_mode = false;
                } else {
                    if (calc_decimal_mode) {
                         // Add digit to fraction
                         if (calc_decimal_divisor <= SCALE) {
                             long long digit_val = ((long long)(lbl - '0') * SCALE) / calc_decimal_divisor;
                             if (calc_curr >= 0) calc_curr += digit_val;
                             else calc_curr -= digit_val;
                             calc_decimal_divisor *= 10;
                         }
                    } else {
                        // Integer shift
                        if (calc_curr >= 0) calc_curr = calc_curr * 10 + (lbl - '0') * SCALE;
                        else calc_curr = calc_curr * 10 - (lbl - '0') * SCALE;
                    }
                }
                calc_error = false;
            } else if (lbl == '.') {
                if (calc_new_entry) {
                    calc_curr = 0;
                    calc_new_entry = false;
                }
                if (!calc_decimal_mode) {
                    calc_decimal_mode = true;
                    calc_decimal_divisor = 10;
                }
            } else if (lbl == 'C') {
                calc_curr = 0;
                calc_acc = 0;
                calc_op = 0;
                calc_new_entry = true;
                calc_error = false;
                calc_decimal_mode = false;
            } else if (lbl == 'B') { // BS (Backspace)
                if (!calc_new_entry && !calc_error) {
                    if (calc_decimal_mode) {
                         calc_curr = 0; 
                         calc_new_entry = true;
                    } else {
                        calc_curr /= 10;
                    }
                }
            } else if (lbl == 's') { // sqr
                calc_curr = (calc_curr * calc_curr) / SCALE;
                calc_new_entry = true;
            } else if (lbl == 'r') { // rt (sqrt)
                long long s = isqrt(calc_curr);
                if (s == -1) calc_error = true;
                else calc_curr = s * 1000;
                calc_new_entry = true;
            } else if (lbl == '=') {
                do_op();
                calc_curr = calc_acc;
                calc_op = 0;
                calc_new_entry = true;
                calc_decimal_mode = false;
            } else {
                // Operator
                if (!calc_new_entry) {
                    if (calc_op) do_op();
                    else calc_acc = calc_curr;
                }
                calc_op = lbl;
                calc_new_entry = true;
                calc_decimal_mode = false;
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
    win_calculator.w = 180; // Slightly wider for 4 cols with gaps
    win_calculator.h = 230; // Taller for 5 rows
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