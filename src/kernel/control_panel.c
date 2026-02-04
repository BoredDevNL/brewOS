#include "control_panel.h"
#include "graphics.h"
#include <stddef.h>
#include "wm.h"

Window win_control_panel;

#define COLOR_COFFEE    0xFF6B4423
#define COLOR_TEAL      0xFF008080
#define COLOR_GREEN     0xFF008000
#define COLOR_BLUE_BG   0xFF000080
#define COLOR_PURPLE    0xFF800080

// Control panel state
#define VIEW_MAIN 0
#define VIEW_WALLPAPER 1

static int current_view = VIEW_MAIN;
static char rgb_r[4] = "";
static char rgb_g[4] = "";
static char rgb_b[4] = "";
static int focused_field = -1;  // -1=none, 0=R, 1=G, 2=B
static int input_cursor = 0;

static uint32_t parse_rgb_separate(const char *r, const char *g, const char *b) {
    int rv = 0, gv = 0, bv = 0;
    
    // Parse R
    for (int i = 0; r[i] && i < 3; i++) {
        if (r[i] >= '0' && r[i] <= '9') {
            rv = rv * 10 + (r[i] - '0');
        }
    }
    
    // Parse G
    for (int i = 0; g[i] && i < 3; i++) {
        if (g[i] >= '0' && g[i] <= '9') {
            gv = gv * 10 + (g[i] - '0');
        }
    }
    
    // Parse B
    for (int i = 0; b[i] && i < 3; i++) {
        if (b[i] >= '0' && b[i] <= '9') {
            bv = bv * 10 + (b[i] - '0');
        }
    }
    
    // Clamp values
    if (rv > 255) rv = 255;
    if (gv > 255) gv = 255;
    if (bv > 255) bv = 255;
    
    return 0xFF000000 | (rv << 16) | (gv << 8) | bv;
}

static void control_panel_paint_main(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Draw wallpaper folder icon
    // Folder icon
    draw_rect(offset_x + 5, offset_y, 15, 6, COLOR_LTGRAY);
    draw_rect(offset_x + 5, offset_y, 15, 1, COLOR_BLACK);
    draw_rect(offset_x + 5, offset_y, 1, 6, COLOR_BLACK);
    draw_rect(offset_x + 19, offset_y, 1, 6, COLOR_BLACK);
    
    draw_rect(offset_x + 5, offset_y + 6, 25, 15, COLOR_LTGRAY);
    draw_rect(offset_x + 5, offset_y + 6, 25, 1, COLOR_BLACK);
    draw_rect(offset_x + 5, offset_y + 6, 1, 15, COLOR_BLACK);
    draw_rect(offset_x + 29, offset_y + 6, 1, 15, COLOR_BLACK);
    draw_rect(offset_x + 5, offset_y + 20, 25, 1, COLOR_BLACK);
    
    // Label
    draw_string(offset_x + 40, offset_y + 8, "Wallpaper", 0xFF000000);
}

static void control_panel_paint_wallpaper(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Back button
    draw_string(offset_x, offset_y, "< Back", 0xFF000080);
    
    draw_string(offset_x, offset_y + 25, "Presets:", 0xFF000000);
    
    // Color buttons
    int button_y = offset_y + 45;
    int button_x = offset_x;
    
    // Coffee button
    draw_button(button_x, button_y, 60, 20, "Coffee", false);
    draw_rect(button_x + 65, button_y + 5, 20, 10, COLOR_COFFEE);
    
    // Teal button
    draw_button(button_x + 100, button_y, 60, 20, "Teal", false);
    draw_rect(button_x + 165, button_y + 5, 20, 10, COLOR_TEAL);
    
    // Green button
    draw_button(button_x + 200, button_y, 60, 20, "Green", false);
    draw_rect(button_x + 265, button_y + 5, 20, 10, COLOR_GREEN);
    
    // Blue button
    button_y += 30;
    draw_button(button_x, button_y, 60, 20, "Blue", false);
    draw_rect(button_x + 65, button_y + 5, 20, 10, COLOR_BLUE_BG);
    
    // Purple button
    draw_button(button_x + 100, button_y, 60, 20, "Purple", false);
    draw_rect(button_x + 165, button_y + 5, 20, 10, COLOR_PURPLE);
    
    // Custom color section
    button_y += 40;
    draw_string(offset_x, button_y, "Or something custom", 0xFF000000);
    
    button_y += 20;
    
    // R input box
    draw_string(button_x, button_y, "R:", 0xFF000000);
    draw_rect(button_x + 25, button_y, 50, 15, 0xFFFFFFFF);
    draw_rect(button_x + 25, button_y, 50, 1, COLOR_BLACK);
    draw_rect(button_x + 25, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 74, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 25, button_y + 14, 50, 1, COLOR_BLACK);
    draw_string(button_x + 30, button_y + 3, rgb_r, (focused_field == 0) ? 0xFFFF0000 : COLOR_BLACK);
    if (focused_field == 0) {
        // Draw cursor
        int cursor_x = button_x + 30 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 3, 1, 9, 0xFFFF0000);
    }
    
    // G input box
    draw_string(button_x + 90, button_y, "G:", 0xFF000000);
    draw_rect(button_x + 115, button_y, 50, 15, 0xFFFFFFFF);
    draw_rect(button_x + 115, button_y, 50, 1, COLOR_BLACK);
    draw_rect(button_x + 115, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 164, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 115, button_y + 14, 50, 1, COLOR_BLACK);
    draw_string(button_x + 120, button_y + 3, rgb_g, (focused_field == 1) ? 0xFF00AA00 : COLOR_BLACK);
    if (focused_field == 1) {
        // Draw cursor
        int cursor_x = button_x + 120 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 3, 1, 9, 0xFF00AA00);
    }
    
    // B input box
    draw_string(button_x + 180, button_y, "B:", 0xFF000000);
    draw_rect(button_x + 205, button_y, 50, 15, 0xFFFFFFFF);
    draw_rect(button_x + 205, button_y, 50, 1, COLOR_BLACK);
    draw_rect(button_x + 205, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 254, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 205, button_y + 14, 50, 1, COLOR_BLACK);
    draw_string(button_x + 210, button_y + 3, rgb_b, (focused_field == 2) ? 0xFF0000FF : COLOR_BLACK);
    if (focused_field == 2) {
        // Draw cursor
        int cursor_x = button_x + 210 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 3, 1, 9, 0xFF0000FF);
    }
    
    // Apply button
    draw_button(button_x, button_y + 25, 70, 20, "Apply", false);
}

static void control_panel_paint(Window *win) {
    if (current_view == VIEW_MAIN) {
        control_panel_paint_main(win);
    } else if (current_view == VIEW_WALLPAPER) {
        control_panel_paint_wallpaper(win);
    }
}

static void control_panel_handle_click(Window *win, int x, int y) {
    (void)win;  // Unused parameter
    
    if (current_view == VIEW_MAIN) {
        int offset_x = 8;
        int offset_y = 30;
        
        // Check wallpaper folder click
        if (x >= offset_x + 5 && x < offset_x + 35 &&
            y >= offset_y && y < offset_y + 25) {
            current_view = VIEW_WALLPAPER;
        }
    } else if (current_view == VIEW_WALLPAPER) {
        int offset_x = 8;
        int offset_y = 30;
        int button_y = offset_y + 45;
        int button_x = offset_x;
        
        // Back button
        if (x >= offset_x && x < offset_x + 40 &&
            y >= offset_y && y < offset_y + 15) {
            current_view = VIEW_MAIN;
            return;
        }
        
        // Check Coffee button
        if (x >= button_x && x < button_x + 60 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_COFFEE);
            return;
        }
        
        // Check Teal button
        if (x >= button_x + 100 && x < button_x + 160 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_TEAL);
            return;
        }
        
        // Check Green button
        if (x >= button_x + 200 && x < button_x + 260 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_GREEN);
            return;
        }
        
        // Check Blue button
        button_y += 30;
        if (x >= button_x && x < button_x + 60 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_BLUE_BG);
            return;
        }
        
        // Check Purple button
        if (x >= button_x + 100 && x < button_x + 160 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_PURPLE);
            return;
        }
        
        // Custom RGB section
        button_y += 40;
        button_y += 20;
        
        // Check R input box click
        if (x >= button_x + 25 && x < button_x + 75 && y >= button_y && y < button_y + 15) {
            if (focused_field != 0) {
                rgb_r[0] = '\0';  // Clear when first focused
            }
            focused_field = 0;
            input_cursor = 0;
            return;
        }
        
        // Check G input box click
        if (x >= button_x + 115 && x < button_x + 165 && y >= button_y && y < button_y + 15) {
            if (focused_field != 1) {
                rgb_g[0] = '\0';  // Clear when first focused
            }
            focused_field = 1;
            input_cursor = 0;
            return;
        }
        
        // Check B input box click
        if (x >= button_x + 205 && x < button_x + 255 && y >= button_y && y < button_y + 15) {
            if (focused_field != 2) {
                rgb_b[0] = '\0';  // Clear when first focused
            }
            focused_field = 2;
            input_cursor = 0;
            return;
        }
        
        // Check Apply button
        if (x >= button_x && x < button_x + 70 && y >= button_y + 25 && y < button_y + 45) {
            graphics_set_bg_color(parse_rgb_separate(rgb_r, rgb_g, rgb_b));
            return;
        }
    }
}

static void control_panel_handle_key(Window *win, char c) {
    (void)win;  // Unused parameter
    
    if (current_view != VIEW_WALLPAPER) return;
    if (focused_field < 0) return;  // No field focused
    
    // Get the currently focused field buffer
    char *focused_buffer = NULL;
    int max_len = 3;  // RGB values are 0-255, max 3 digits
    
    if (focused_field == 0) {
        focused_buffer = rgb_r;
    } else if (focused_field == 1) {
        focused_buffer = rgb_g;
    } else if (focused_field == 2) {
        focused_buffer = rgb_b;
    } else {
        return;
    }
    
    if (c == '\b') {  // Backspace
        if (input_cursor > 0) {
            input_cursor--;
            focused_buffer[input_cursor] = '\0';
        }
    } else if (c >= '0' && c <= '9') {  // Digits only
        if (input_cursor < max_len) {
            focused_buffer[input_cursor] = c;
            input_cursor++;
            focused_buffer[input_cursor] = '\0';
        }
    } else if (c == '\t') {  // Tab - switch to next field
        focused_field = (focused_field + 1) % 3;
        input_cursor = 0;
    }
}

void control_panel_init(void) {
    win_control_panel.title = "Control Panel";
    win_control_panel.x = 200;
    win_control_panel.y = 150;
    win_control_panel.w = 350;
    win_control_panel.h = 300;
    win_control_panel.visible = false;
    win_control_panel.focused = false;
    win_control_panel.z_index = 0;
    win_control_panel.paint = control_panel_paint;
    win_control_panel.handle_key = control_panel_handle_key;
    win_control_panel.handle_click = control_panel_handle_click;
    win_control_panel.handle_right_click = NULL;
    win_control_panel.buf_len = 0;
    win_control_panel.cursor_pos = 0;
}

void control_panel_reset(void) {
    win_control_panel.focused = false;
    current_view = VIEW_MAIN;
    focused_field = -1;
    input_cursor = 0;
}
