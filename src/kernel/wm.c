#include "wm.h"
#include "graphics.h"
#include "io.h"
#include "cmd.h"
#include "calculator.h"
#include "cli_apps/cli_utils.h"
#include "explorer.h"
#include "editor.h"
#include <stdbool.h>
#include <stddef.h>
#include "notepad.h"
#include "control_panel.h"
#include "about.h"
#include "minesweeper.h"

// --- State ---
static int mx = 400, my = 300; // Mouse Pos
static int prev_mx = 400, prev_my = 300; // Previous mouse position
static bool start_menu_open = false;

// Dragging State
static bool is_dragging = false;
static Window *drag_window = NULL;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

// Windows array for z-order management
static Window *all_windows[8];
static int window_count = 0;

// Redraw system
static bool force_redraw = true;  // Force full redraw on next tick
static uint32_t timer_ticks = 0;

// Cursor state
static bool cursor_visible = true;
static int last_cursor_x = 400;
static int last_cursor_y = 300;

// --- Drawing Helpers ---

// Draw a bevelled box (Win 3.1 style)
void draw_bevel_rect(int x, int y, int w, int h, bool sunken) {
    draw_rect(x, y, w, h, COLOR_GRAY);
    
    uint32_t top_left = sunken ? COLOR_DKGRAY : COLOR_WHITE;
    uint32_t bot_right = sunken ? COLOR_WHITE : COLOR_DKGRAY;
    
    // Top
    draw_rect(x, y, w, 1, top_left);
    // Left
    draw_rect(x, y, 1, h, top_left);
    // Bottom
    draw_rect(x, y + h - 1, w, 1, bot_right);
    // Right
    draw_rect(x + w - 1, y, 1, h, bot_right);
}

void draw_button(int x, int y, int w, int h, const char *text, bool pressed) {
    draw_bevel_rect(x, y, w, h, pressed);
    // Center Text
    int len = 0; while(text[len]) len++;
    int tx = x + (w - (len * 8)) / 2;
    int ty = y + (h - 8) / 2;
    if (pressed) { tx++; ty++; }
    draw_string(tx, ty, text, COLOR_BLACK);
}

void draw_coffee_cup(int x, int y, int size) {
    // Coffee cup icon - small retro style
    int cup_w = size;
    int cup_h = size - 2;
    
    // Cup body (tan/cream color)
    draw_rect(x + 1, y + 2, cup_w - 2, cup_h - 3, COLOR_LTGRAY);
    
    // Cup outline
    draw_rect(x + 1, y + 2, cup_w - 2, 1, COLOR_BLACK);  // Top
    draw_rect(x + 1, y + 2, 1, cup_h - 3, COLOR_BLACK);  // Left
    draw_rect(x + cup_w - 2, y + 2, 1, cup_h - 3, COLOR_BLACK);  // Right
    draw_rect(x + 1, y + cup_h - 1, cup_w - 2, 1, COLOR_BLACK);  // Bottom
    
    // Rounded bottom corners
    draw_rect(x + 1, y + cup_h - 1, 1, 1, COLOR_LTGRAY);
    draw_rect(x + cup_w - 2, y + cup_h - 1, 1, 1, COLOR_LTGRAY);
    
    // Handle - much bigger (on the right side, pointing inward)
    draw_rect(x + cup_w, y + 3, 2, 8, COLOR_BLACK);
    draw_rect(x + cup_w - 2, y + 3, 2, 1, COLOR_BLACK);
    draw_rect(x + cup_w - 2, y + 10, 2, 1, COLOR_BLACK);
    
    // Coffee liquid inside - rainbow Apple logo stripes (blue, green, yellow, red, purple, blue)
    int stripe_height = (cup_h - 5) / 6;
    int coffee_y = y + 4;
    draw_rect(x + 2, coffee_y, cup_w - 4, stripe_height, COLOR_APPLE_BLUE);
    draw_rect(x + 2, coffee_y + stripe_height, cup_w - 4, stripe_height, COLOR_APPLE_GREEN);
    draw_rect(x + 2, coffee_y + stripe_height * 2, cup_w - 4, stripe_height, COLOR_APPLE_YELLOW);
    draw_rect(x + 2, coffee_y + stripe_height * 3, cup_w - 4, stripe_height, COLOR_APPLE_RED);
    draw_rect(x + 2, coffee_y + stripe_height * 4, cup_w - 4, stripe_height, COLOR_APPLE_VIOLET);
    draw_rect(x + 2, coffee_y + stripe_height * 5, cup_w - 4, stripe_height, COLOR_APPLE_BLUE);
}

void draw_icon(int x, int y, const char *label) {
    // Simple "File" Icon
    draw_rect(x + 10, y, 20, 25, COLOR_WHITE);
    draw_rect(x + 10, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 10, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 30, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 10, y + 25, 21, 1, COLOR_BLACK);
    
    // Label
    draw_string(x, y + 30, label, COLOR_WHITE);
}

void draw_folder_icon(int x, int y, const char *label) {
    // Folder icon (yellow folder)
    // Folder tab
    draw_rect(x + 5, y, 15, 6, COLOR_LTGRAY);
    draw_rect(x + 5, y, 15, 1, COLOR_BLACK);
    draw_rect(x + 5, y, 1, 6, COLOR_BLACK);
    draw_rect(x + 19, y, 1, 6, COLOR_BLACK);
    
    // Folder body
    draw_rect(x + 5, y + 6, 25, 15, COLOR_LTGRAY);
    draw_rect(x + 5, y + 6, 25, 1, COLOR_BLACK);
    draw_rect(x + 5, y + 6, 1, 15, COLOR_BLACK);
    draw_rect(x + 29, y + 6, 1, 15, COLOR_BLACK);
    draw_rect(x + 5, y + 20, 25, 1, COLOR_BLACK);
    
    // Label
    draw_string(x, y + 30, label, COLOR_WHITE);
}

void draw_document_icon(int x, int y, const char *label) {
    // Document icon (white paper with lines)
    draw_rect(x + 10, y, 20, 25, COLOR_WHITE);
    draw_rect(x + 10, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 10, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 30, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 10, y + 25, 21, 1, COLOR_BLACK);
    
    // Lines on document
    draw_rect(x + 14, y + 8, 12, 1, COLOR_BLACK);
    draw_rect(x + 14, y + 12, 12, 1, COLOR_BLACK);
    draw_rect(x + 14, y + 16, 12, 1, COLOR_BLACK);
    
    // Label
    draw_string(x, y + 30, label, COLOR_WHITE);
}

void draw_window(Window *win) {
    if (!win->visible) return;
    
    // Main Body
    draw_bevel_rect(win->x, win->y, win->w, win->h, false);
    
    // Title Bar
    uint32_t title_color = win->focused ? COLOR_BLUE : COLOR_DKGRAY;
    draw_rect(win->x + 3, win->y + 3, win->w - 6, 18, title_color);
    draw_string(win->x + 8, win->y + 8, win->title, COLOR_WHITE);
    
    // Close Button (X)
    draw_button(win->x + win->w - 20, win->y + 5, 14, 14, "X", false);
    
    // Client Area
    draw_rect(win->x + 4, win->y + 24, win->w - 8, win->h - 28, COLOR_WHITE);
    
    if (win->paint) {
        win->paint(win);
    }
}

// Draw Mouse Cursor (Simple Arrow)
void draw_cursor(int x, int y) {
    // 0 = Transparent (skip), 1 = Black, 2 = White
    static const uint8_t cursor_bitmap[10][10] = {
        {1,1,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0},
        {1,2,2,1,1,1,1,0,0,0},
        {1,1,1,0,1,2,1,0,0,0},
        {0,0,0,0,0,1,2,1,0,0},
        {0,0,0,0,0,0,1,0,0,0}
    };
    
    for (int r = 0; r < 10; r++) {
        for (int c = 0; c < 10; c++) {
            uint8_t p = cursor_bitmap[r][c];
            if (p == 1) put_pixel(x + c, y + r, COLOR_BLACK);
            else if (p == 2) put_pixel(x + c, y + r, COLOR_WHITE);
        }
    }
}

// Erase cursor by redrawing the background in that area
static void erase_cursor(int x, int y) {
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    // Clamp to screen
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + 10 > sw ? sw : x + 10;
    int y2 = y + 10 > sh ? sh : y + 10;
    int w = x2 - x1;
    int h = y2 - y1;
    
    // Check what's underneath the cursor and redraw it
    if (y1 < sh - 28) {
        // Desktop or window area - draw teal background
        draw_rect(x1, y1, w, h, COLOR_TEAL);
    } else {
        // Taskbar area - draw gray background
        draw_rect(x1, y1, w, h, COLOR_GRAY);
    }
}

// --- Clock ---
static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static void draw_clock(int x, int y) {
    // Wait for update in progress
    while (rtc_read(0x0A) & 0x80);

    uint8_t s = rtc_read(0x00);
    uint8_t m = rtc_read(0x02);
    uint8_t h = rtc_read(0x04);
    uint8_t b = rtc_read(0x0B);

    if (!(b & 0x04)) {
        s = (s & 0x0F) + ((s >> 4) * 10);
        m = (m & 0x0F) + ((m >> 4) * 10);
        h = (h & 0x0F) + ((h >> 4) * 10);
    }

    char buf[9];
    buf[0] = '0' + (h / 10);
    buf[1] = '0' + (h % 10);
    buf[2] = ':';
    buf[3] = '0' + (m / 10);
    buf[4] = '0' + (m % 10);
    buf[5] = ':';
    buf[6] = '0' + (s / 10);
    buf[7] = '0' + (s % 10);
    buf[8] = 0;

    draw_string(x, y, buf, COLOR_BLACK);
}

// --- Main Paint Function ---
void wm_paint(void) {
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    // First, erase the old cursor (before redrawing anything)
    if (cursor_visible) {
        erase_cursor(last_cursor_x, last_cursor_y);
    }
    
    // 1. Desktop
    draw_desktop_background();
    
    draw_folder_icon(20, 20, "Explorer");
    draw_document_icon(20, 80, "Notepad");
    
    // 3. Windows - sort by z-index and draw
    // Simple bubble sort by z-index (5 windows max)
    Window *sorted_windows[6];
    for (int i = 0; i < window_count; i++) {
        sorted_windows[i] = all_windows[i];
    }
    
    for (int i = 0; i < window_count - 1; i++) {
        for (int j = 0; j < window_count - i - 1; j++) {
            if (sorted_windows[j]->z_index > sorted_windows[j + 1]->z_index) {
                Window *temp = sorted_windows[j];
                sorted_windows[j] = sorted_windows[j + 1];
                sorted_windows[j + 1] = temp;
            }
        }
    }
    
    // Draw windows in z-order (lowest first)
    for (int i = 0; i < window_count; i++) {
        draw_window(sorted_windows[i]);
    }
    
    // 4. Taskbar
    draw_rect(0, sh - 28, sw, 28, COLOR_GRAY);
    draw_rect(0, sh - 28, sw, 2, COLOR_WHITE); // Top highlight
    
    // 5. Start Button with Coffee Cup Icon
    draw_bevel_rect(2, sh - 26, 90, 24, start_menu_open);
    // Draw coffee cup icon on the button
    draw_coffee_cup(5, sh - 24, 20);
    // Draw BrewOS text with extra spacing on the left
    draw_string(35, sh - 18, "BrewOS", COLOR_BLACK);
    
    // Clock
    draw_clock(sw - 80, sh - 20);
    
    // 6. Start Menu (if open)
    if (start_menu_open) {
        int menu_h = 230;
        int menu_y = sh - 28 - menu_h;
        draw_bevel_rect(0, menu_y, 120, menu_h, false);
        
        // Items
        draw_string(8, menu_y + 8, "Explorer", COLOR_BLACK);
        draw_string(8, menu_y + 28, "Notepad", COLOR_BLACK);
        draw_string(8, menu_y + 48, "Editor", COLOR_BLACK);
        draw_string(8, menu_y + 68, "CMD", COLOR_BLACK);
        draw_string(8, menu_y + 88, "Calculator", COLOR_BLACK);
        draw_string(8, menu_y + 108, "Minesweeper", COLOR_BLACK);
        draw_string(8, menu_y + 128, "Control Panel", COLOR_BLACK);
        draw_string(8, menu_y + 148, "About BrewOS", COLOR_BLACK);
        
        // Separator line
        draw_rect(5, menu_y + 165, 110, 1, COLOR_BLACK);
        
        // Power options at bottom
        draw_string(8, menu_y + 175, "Shutdown", COLOR_BLACK);
        draw_string(8, menu_y + 195, "Restart", COLOR_BLACK);
    }
    
    // 7. Mouse cursor (draw last so it's on top)
    draw_cursor(mx, my);
    last_cursor_x = mx;
    last_cursor_y = my;
    
    // Flip the buffer - display the rendered frame atomically
    graphics_flip_buffer();
}

// --- Input Handling ---

bool rect_contains(int x, int y, int w, int h, int px, int py) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void wm_handle_click(int x, int y) {
    int sh = get_screen_height();
    
    // Check Start Button
    if (rect_contains(2, sh - 26, 90, 24, x, y)) {
        start_menu_open = !start_menu_open;
        force_redraw = true;
        return;
    }
    
    // Check Start Menu Items
    if (start_menu_open) {
        int menu_h = 230;
        int menu_y = sh - 28 - menu_h;
        if (rect_contains(0, menu_y, 120, menu_h, x, y)) {
            // Clear focus from all windows first
            for (int i = 0; i < window_count; i++) {
                all_windows[i]->focused = false;
            }
            
            // Find which item was clicked
            if (y < menu_y + 25) { // Explorer
                win_explorer.visible = true;
                win_explorer.focused = true;
                // Bring to front
                int max_z = 0;
                for (int i = 0; i < window_count; i++) {
                    if (all_windows[i]->z_index > max_z) {
                        max_z = all_windows[i]->z_index;
                    }
                }
                win_explorer.z_index = max_z + 1;
                start_menu_open = false;
            } else if (y < menu_y + 45) { // Notepad
                win_notepad.visible = true;
                win_notepad.focused = true;
                int max_z = 0;
                for (int i = 0; i < window_count; i++) {
                    if (all_windows[i]->z_index > max_z) {
                        max_z = all_windows[i]->z_index;
                    }
                }
                win_notepad.z_index = max_z + 1;
                start_menu_open = false;
            } else if (y < menu_y + 65) { // Editor
                win_editor.visible = true;
                win_editor.focused = true;
                int max_z = 0;
                for (int i = 0; i < window_count; i++) {
                    if (all_windows[i]->z_index > max_z) {
                        max_z = all_windows[i]->z_index;
                    }
                }
                win_editor.z_index = max_z + 1;
                start_menu_open = false;
            } else if (y < menu_y + 85) { // CMD
                win_cmd.visible = true;
                win_cmd.focused = true;
                int max_z = 0;
                for (int i = 0; i < window_count; i++) {
                    if (all_windows[i]->z_index > max_z) {
                        max_z = all_windows[i]->z_index;
                    }
                }
                win_cmd.z_index = max_z + 1;
                start_menu_open = false;
            } else if (y < menu_y + 105) { // Calculator
                win_calculator.visible = true;
                win_calculator.focused = true;
                int max_z = 0;
                for (int i = 0; i < window_count; i++) {
                    if (all_windows[i]->z_index > max_z) {
                        max_z = all_windows[i]->z_index;
                    }
                }
                win_calculator.z_index = max_z + 1;
                start_menu_open = false;
            } else if (y < menu_y + 125) { // Minesweeper
                win_minesweeper.visible = true;
                win_minesweeper.focused = true;
                int max_z = 0;
                for (int i = 0; i < window_count; i++) {
                    if (all_windows[i]->z_index > max_z) {
                        max_z = all_windows[i]->z_index;
                    }
                }
                win_minesweeper.z_index = max_z + 1;
                start_menu_open = false;
            } else if (y < menu_y + 145) { // Control Panel
                win_control_panel.visible = true;
                win_control_panel.focused = true;
                int max_z = 0;
                for (int i = 0; i < window_count; i++) {
                    if (all_windows[i]->z_index > max_z) {
                        max_z = all_windows[i]->z_index;
                    }
                }
                win_control_panel.z_index = max_z + 1;
                start_menu_open = false;
            } else if (y < menu_y + 165) { // About BrewOS
                win_about.visible = true;
                win_about.focused = true;
                int max_z = 0;
                for (int i = 0; i < window_count; i++) {
                    if (all_windows[i]->z_index > max_z) {
                        max_z = all_windows[i]->z_index;
                    }
                }
                win_about.z_index = max_z + 1;
                start_menu_open = false;
            } else if (y < menu_y + 185) { // Shutdown
                cli_cmd_shutdown(NULL);
                start_menu_open = false;
            } else { // Restart
                cli_cmd_reboot(NULL);
                start_menu_open = false;
            }
            force_redraw = true;
            return;
        }
    }
    
    // Find topmost window at click location
    Window *topmost = NULL;
    int topmost_z = -1;
    
    for (int i = 0; i < window_count; i++) {
        Window *win = all_windows[i];
        if (win->visible && rect_contains(win->x, win->y, win->w, win->h, x, y)) {
            if (win->z_index > topmost_z) {
                topmost = win;
                topmost_z = win->z_index;
            }
        }
    }
    
    // If a window was clicked
    if (topmost != NULL) {
        // Clear focus from all windows
        for (int i = 0; i < window_count; i++) {
            all_windows[i]->focused = false;
        }
        
        // Bring it to front
        int max_z = 0;
        for (int i = 0; i < window_count; i++) {
            if (all_windows[i]->z_index > max_z) {
                max_z = all_windows[i]->z_index;
            }
        }
        topmost->z_index = max_z + 1;
        topmost->focused = true;
        
        // Check close button
        if (rect_contains(topmost->x + topmost->w - 20, topmost->y + 5, 14, 14, x, y)) {
            topmost->visible = false;
            // Reset window state on close
            if (topmost == &win_explorer) {
                explorer_reset();
            } else if (topmost == &win_notepad) {
                notepad_reset();
            } else if (topmost == &win_control_panel) {
                control_panel_reset();
            }
        } else if (y < topmost->y + 24) {
            // Dragging the title bar
            is_dragging = true;
            drag_window = topmost;
            drag_offset_x = x - topmost->x;
            drag_offset_y = y - topmost->y;
        } else {
            // Content click
            if (topmost->handle_click) {
                topmost->handle_click(topmost, x - topmost->x, y - topmost->y);
            }
        }
    } else {
        // No window clicked - check desktop icons
        // Clear focus from all windows first
        for (int i = 0; i < window_count; i++) {
            all_windows[i]->focused = false;
        }
        
        if (rect_contains(20, 20, 40, 40, x, y)) {
            // Explorer icon
            win_explorer.visible = true;
            win_explorer.focused = true;
            int max_z = 0;
            for (int i = 0; i < window_count; i++) {
                if (all_windows[i]->z_index > max_z) {
                    max_z = all_windows[i]->z_index;
                }
            }
            win_explorer.z_index = max_z + 1;
        } else if (rect_contains(20, 80, 40, 40, x, y)) {
            // Notepad icon
            win_notepad.visible = true;
            win_notepad.focused = true;
            int max_z = 0;
            for (int i = 0; i < window_count; i++) {
                if (all_windows[i]->z_index > max_z) {
                    max_z = all_windows[i]->z_index;
                }
            }
            win_notepad.z_index = max_z + 1;
        } else if (rect_contains(20, 140, 40, 40, x, y)) {
            win_calculator.visible = true;
            win_calculator.focused = true;
            int max_z = 0;
            for (int i = 0; i < window_count; i++) {
                if (all_windows[i]->z_index > max_z) {
                    max_z = all_windows[i]->z_index;
                }
            }
            win_calculator.z_index = max_z + 1;
        } else if (rect_contains(20, 200, 40, 40, x, y)) {
            win_explorer.visible = true;
            win_explorer.focused = true;
            int max_z = 0;
            for (int i = 0; i < window_count; i++) {
                if (all_windows[i]->z_index > max_z) {
                    max_z = all_windows[i]->z_index;
                }
            }
            win_explorer.z_index = max_z + 1;
        } else if (rect_contains(20, 260, 40, 40, x, y)) {
            win_editor.visible = true;
            win_editor.focused = true;
            int max_z = 0;
            for (int i = 0; i < window_count; i++) {
                if (all_windows[i]->z_index > max_z) {
                    max_z = all_windows[i]->z_index;
                }
            }
            win_editor.z_index = max_z + 1;
        }
    }
    
    // Close start menu if clicked elsewhere
    if (start_menu_open) {
        start_menu_open = false;
    }
    
    force_redraw = true;
}

// Handle right click (context menu or special actions)
void wm_handle_right_click(int x, int y) {
    int sh = get_screen_height();
    
    // Find topmost window at click location
    Window *topmost = NULL;
    int topmost_z = -1;
    
    for (int i = 0; i < window_count; i++) {
        Window *win = all_windows[i];
        if (win->visible && rect_contains(win->x, win->y, win->w, win->h, x, y)) {
            if (win->z_index > topmost_z) {
                topmost = win;
                topmost_z = win->z_index;
            }
        }
    }
    
    // If a window was clicked
    if (topmost != NULL) {
        // Don't process close button or title bar for right click
        if (y >= topmost->y + 24) {
            // Content right click
            if (topmost->handle_right_click) {
                topmost->handle_right_click(topmost, x - topmost->x, y - topmost->y);
            }
        }
    }
    
    force_redraw = true;
}void wm_handle_mouse(int dx, int dy, uint8_t buttons) {
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    prev_mx = mx;
    prev_my = my;
    
    mx += dx;
    my += dy;
    
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= sw) mx = sw - 1;
    if (my >= sh) my = sh - 1;
    
    static bool prev_left = false;
    static bool prev_right = false;
    bool left = buttons & 0x01;
    bool right = buttons & 0x02;
    
    if (left && !prev_left) {
        wm_handle_click(mx, my);
    } else if (right && !prev_right) {
        wm_handle_right_click(mx, my);
    } else if (left && is_dragging && drag_window) {
        drag_window->x = mx - drag_offset_x;
        drag_window->y = my - drag_offset_y;
        // Mark for full redraw since window moved
        force_redraw = true;
    } else if (left && !is_dragging && (dx != 0 || dy != 0)) {
        // Mouse is moving while left button is held, but window isn't being dragged
        force_redraw = true;
    } else if (!left && is_dragging) {
        is_dragging = false;
        drag_window = NULL;
        // Mark for full redraw since dragging ended
        force_redraw = true;
    }
    
    prev_left = left;
    prev_right = right;
    
    if (prev_mx != mx || prev_my != my) {
        // Cursor moved - just mark dirty cursor areas
        wm_mark_dirty(prev_mx, prev_my, 10, 10);
        wm_mark_dirty(mx, my, 10, 10);
    }
    
    prev_left = left;
}

void wm_handle_key(char c) {
    Window *target = NULL;
    if (win_notepad.focused && win_notepad.visible) target = &win_notepad;
    else if (win_cmd.focused && win_cmd.visible) target = &win_cmd;
    else if (win_calculator.focused && win_calculator.visible) target = &win_calculator;
    else if (win_explorer.focused && win_explorer.visible) target = &win_explorer;
    else if (win_editor.focused && win_editor.visible) target = &win_editor;
    else if (win_control_panel.focused && win_control_panel.visible) target = &win_control_panel;
    
    if (!target) return;
    
    if (target->handle_key) {
        target->handle_key(target, c);
    }
    
    // Mark window as needing redraw on next timer tick
    wm_mark_dirty(target->x, target->y, target->w, target->h);
}

void wm_mark_dirty(int x, int y, int w, int h) {
    graphics_mark_dirty(x, y, w, h);
}

void wm_refresh(void) {
    force_redraw = true;
}

void wm_init(void) {
    notepad_init();
    cmd_init();
    calculator_init();
    explorer_init();
    editor_init();
    control_panel_init();
    about_init();
    minesweeper_init();
    
    // Initialize z-indices
    win_notepad.z_index = 0;
    win_cmd.z_index = 1;
    win_calculator.z_index = 2;
    win_explorer.z_index = 3;
    win_editor.z_index = 4;
    win_control_panel.z_index = 5;
    win_about.z_index = 6;
    win_minesweeper.z_index = 7;
    
    // Register windows in array
    all_windows[0] = &win_notepad;
    all_windows[1] = &win_cmd;
    all_windows[2] = &win_calculator;
    all_windows[3] = &win_explorer;
    all_windows[4] = &win_editor;
    all_windows[5] = &win_control_panel;
    all_windows[6] = &win_about;
    all_windows[7] = &win_minesweeper;
    window_count = 8;
    
    // Only show Explorer and Notepad on desktop (Explorer on top)
    win_explorer.visible = false;
    win_explorer.focused = false;
    win_explorer.z_index = 10;
    
    win_notepad.visible = false;
    win_notepad.focused = false;
    win_notepad.z_index = 9;
    
    // Rest are hidden initially
    win_cmd.visible = false;
    win_calculator.visible = false;
    win_editor.visible = false;
    win_control_panel.visible = false;
    win_about.visible = false;
    win_minesweeper.visible = false;
    
    force_redraw = true;
}

// Called by timer interrupt ~60Hz
void wm_timer_tick(void) {
    timer_ticks++;
    
    // Only redraw if there are dirty areas (clock updates at most every second, cursor rarely moves in timer only)
    // Most of the time, nothing changes between ticks
    
    static uint8_t last_second = 0xFF;
    
    outb(0x70, 0x00);
    uint8_t current_sec = inb(0x71);
    
    if (current_sec != last_second) {
        last_second = current_sec;
        int sw = get_screen_width();
        int sh = get_screen_height();
        // Mark clock area + a bit of buffer
        wm_mark_dirty(sw - 90, sh - 30, 90, 20);
    }
    
    // If force_redraw is set, do a full redraw
    if (force_redraw) {
        graphics_mark_screen_dirty();
        force_redraw = false;
    }
    
    // Perform redraw if there are dirty areas
    DirtyRect dirty = graphics_get_dirty_rect();
    if (dirty.active) {
        wm_paint();
        graphics_clear_dirty();
    }
}
