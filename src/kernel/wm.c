#include "wm.h"
#include "graphics.h"
#include "io.h"
#include "cmd.h"
#include "calculator.h"
#include "cli_apps/cli_utils.h"
#include "explorer.h"
#include "editor.h"
#include "markdown.h"
#include <stdbool.h>
#include <stddef.h>
#include "notepad.h"
#include "control_panel.h"
#include "about.h"
#include "minesweeper.h"
#include "fat32.h"
#include "memory_manager.h"

// --- State ---
static int mx = 400, my = 300; // Mouse Pos
static int prev_mx = 400, prev_my = 300; // Previous mouse position
static bool start_menu_open = false;
static char *start_menu_pending_app = NULL; // For click vs drag detection
static int pending_desktop_icon_click = -1; // For desktop icon click vs drag

// Desktop Context Menu
static bool desktop_menu_visible = false;
static int desktop_menu_x = 0;
static int desktop_menu_y = 0;
static int desktop_menu_target_icon = -1; // -1 for background

// Message Box
static bool msg_box_visible = false;
static char msg_box_title[64];
static char msg_box_text[64];

// Hook definition
void (*wm_custom_paint_hook)(void) = NULL;

// Dragging State
static bool is_dragging = false;
static Window *drag_window = NULL;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

// File Dragging State
static bool is_dragging_file = false;
static char drag_file_path[256];
static int drag_icon_type = 0; // 0=File, 1=Folder, 2=App
static int drag_start_x = 0;
static int drag_start_y = 0;
static int drag_icon_orig_x = 0;
static int drag_icon_orig_y = 0;

// Windows array for z-order management
static Window *all_windows[9];
static int window_count = 0;

// Redraw system
static bool force_redraw = true;  // Force full redraw on next tick
static uint32_t timer_ticks = 0;
static int desktop_refresh_timer = 0;

// Cursor state
static bool cursor_visible = true;
static int last_cursor_x = 400;
static int last_cursor_y = 300;

// --- Desktop State ---
#define MAX_DESKTOP_ICONS 32
typedef struct {
    char name[64];
    int x, y;
    int type; // 0=File, 1=Folder, 2=App
    bool selected;
} DesktopIcon;

static DesktopIcon desktop_icons[MAX_DESKTOP_ICONS];
static int desktop_icon_count = 0;

// Desktop Settings
bool desktop_snap_to_grid = true;
bool desktop_auto_align = true;
int desktop_max_rows_per_col = 9;
int desktop_max_cols = 15;

// Helper to check if string ends with suffix
static bool str_ends_with(const char *str, const char *suffix) {
    int str_len = 0; while(str[str_len]) str_len++;
    int suf_len = 0; while(suffix[suf_len]) suf_len++;
    if (suf_len > str_len) return false;
    
    for (int i = 0; i < suf_len; i++) {
        if (str[str_len - suf_len + i] != suffix[i]) return false;
    }
    return true;
}

// Helper to check if string starts with prefix
static bool str_starts_with(const char *str, const char *prefix) {
    while(*prefix) {
        if (*prefix++ != *str++) return false;
    }
    return true;
}

static int str_eq(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void refresh_desktop_icons(void) {
    // Update limit in FS
    fat32_set_desktop_limit(desktop_max_cols * desktop_max_rows_per_col);

    FAT32_FileInfo *files = (FAT32_FileInfo*)kmalloc(MAX_DESKTOP_ICONS * sizeof(FAT32_FileInfo));
    if (!files) return;

    int file_count = fat32_list_directory("/Desktop", files, MAX_DESKTOP_ICONS);
    
    // Temp array to hold new state
    DesktopIcon new_icons[MAX_DESKTOP_ICONS];
    int new_count = 0;
    bool file_processed[MAX_DESKTOP_ICONS];
    for(int i=0; i<MAX_DESKTOP_ICONS; i++) file_processed[i] = false;

    // 1. Preserve existing icons in their current order
    for (int i = 0; i < desktop_icon_count; i++) {
        // Find if this icon still exists in the file list
        int found_idx = -1;
        for (int j = 0; j < file_count; j++) {
            if (!file_processed[j] && str_eq(desktop_icons[i].name, files[j].name) == 0) {
                found_idx = j;
                break;
            }
        }
        
        if (found_idx != -1) {
            // Keep it
            if (new_count < MAX_DESKTOP_ICONS) {
                new_icons[new_count] = desktop_icons[i];
                new_count++;
                file_processed[found_idx] = true;
            }
        }
    }

    // 2. Add new files (not currently on desktop) to the end
    for (int i = 0; i < file_count; i++) {
        if (!file_processed[i]) {
            if (files[i].name[0] == '.') continue; // Skip . and ..
            if (new_count >= MAX_DESKTOP_ICONS) break;

            DesktopIcon *dest = &new_icons[new_count];
            int k = 0; while(files[i].name[k] && k < 63) { dest->name[k] = files[i].name[k]; k++; }
            dest->name[k] = 0;

            if (files[i].is_directory) dest->type = 1;
            else if (str_ends_with(dest->name, ".shortcut")) dest->type = 2;
            else dest->type = 0;

            dest->selected = false;
            dest->x = -1; // Mark as new for layout
            dest->y = -1;
            
            new_count++;
        }
    }
    
    desktop_icon_count = new_count;
    for(int i=0; i<new_count; i++) desktop_icons[i] = new_icons[i];
    kfree(files);
    
    // 3. Layout Icons
    if (desktop_auto_align) {
        int start_x = 20;
        int start_y = 20;
        int grid_x = 0;
        int grid_y = 0;
        
        // Find Recycle Bin index
        int recycle_idx = -1;
        for (int i = 0; i < desktop_icon_count; i++) {
            if (str_starts_with(desktop_icons[i].name, "Recycle Bin")) {
                recycle_idx = i;
                break;
            }
        }
        
        // Place Recycle Bin at bottom-right of grid
        if (recycle_idx != -1) {
            desktop_icons[recycle_idx].x = start_x + (desktop_max_cols - 1) * 80;
            desktop_icons[recycle_idx].y = start_y + (desktop_max_rows_per_col - 1) * 80;
        }
        
        for (int i = 0; i < desktop_icon_count; i++) {
            if (i == recycle_idx) continue;
            
            desktop_icons[i].x = start_x + (grid_x * 80);
            desktop_icons[i].y = start_y + (grid_y * 80);
            
            grid_y++;
            if (grid_y >= desktop_max_rows_per_col) {
                grid_y = 0;
                grid_x++;
                // Skip if we reached the recycle bin column/row? 
                // No, just fill. If we reach max files, we stop adding.
            }
        }
    } else {
        // Place new icons in first available spot
        bool occupied[16][16] = {false};
        for (int i = 0; i < desktop_icon_count; i++) {
            if (desktop_icons[i].x != -1) {
                int col = (desktop_icons[i].x - 20) / 80;
                int row = (desktop_icons[i].y - 20) / 80;
                if (col >= 0 && col < 16 && row >= 0 && row < 16) occupied[col][row] = true;
            }
        }
        
        for (int i = 0; i < desktop_icon_count; i++) {
            if (desktop_icons[i].x == -1) {
                int found_col = -1, found_row = -1;
                for (int c = 0; c < 16; c++) {
                    for (int r = 0; r < desktop_max_rows_per_col; r++) {
                        if (!occupied[c][r]) {
                            found_col = c; found_row = r;
                            goto found;
                        }
                    }
                }
                found:
                if (found_col != -1) {
                    desktop_icons[i].x = 20 + found_col * 80;
                    desktop_icons[i].y = 20 + found_row * 80;
                    occupied[found_col][found_row] = true;
                }
            }
        }
    }
}

void wm_refresh_desktop(void) {
    refresh_desktop_icons();
    force_redraw = true;
}

static void create_desktop_shortcut(const char *app_name) {
    char path[128] = "/Desktop/";
    int p = 9;
    int n = 0; while(app_name[n]) path[p++] = app_name[n++];
    const char *ext = ".shortcut";
    int e = 0; while(ext[e]) path[p++] = ext[e++];
    path[p] = 0;
    
    FAT32_FileHandle *fh = fat32_open(path, "w");
    if (fh) fat32_close(fh);
    refresh_desktop_icons();
}

int wm_get_desktop_icon_count(void) {
    return desktop_icon_count;
}

void wm_show_message(const char *title, const char *message) {
    int i=0; while(title[i] && i<63) { msg_box_title[i] = title[i]; i++; } msg_box_title[i] = 0;
    i=0; while(message[i] && i<63) { msg_box_text[i] = message[i]; i++; } msg_box_text[i] = 0;
    msg_box_visible = true;
    force_redraw = true;
}

static void draw_icon_label(int x, int y, const char *label) {
    char line1[10] = {0};
    char line2[10] = {0};
    int len = 0; while(label[len]) len++;
    
    if (len <= 8) {
        int i=0; while(i<len) { line1[i] = label[i]; i++; }
        line1[i] = 0;
    } else {
        int split = 8;
        // Smart wrap: look for separator in first 8 chars (backwards)
        // Prioritize keeping extension together (e.g. SEVENCH.MD -> split at 7)
        int best_split = -1;
        for (int i = 7; i >= 1; i--) {
            if (label[i] == ' ' || label[i] == '.') {
                best_split = i;
                break;
            }
        }
        
        if (best_split != -1) {
            split = best_split;
        }
        
        // Copy line 1
        int i;
        for (i = 0; i < split; i++) line1[i] = label[i];
        line1[i] = 0;
        
        // Copy line 2
        int start2 = split;
        if (label[split] == ' ') start2++; // Skip space at start of line 2
        
        int j = 0;
        while (label[start2 + j] && j < 8) {
            line2[j] = label[start2 + j];
            j++;
        }
        line2[j] = 0;
        
        // Truncate with .. if longer than 16 (or if line 2 overflows)
        if (label[start2 + j] != 0) {
            if (j > 6) { line2[6] = '.'; line2[7] = '.'; line2[8] = 0; }
            else { line2[j++] = '.'; line2[j++] = '.'; line2[j] = 0; }
        }
    }
    
    // Draw Line 1 Centered in 80px cell
    int l1_len = 0; while(line1[l1_len]) l1_len++;
    int l1_w = l1_len * 8;
    // x passed is cell left. Center is x + 40. Text start is x + 40 - w/2
    draw_string(x + (80 - l1_w)/2, y + 30, line1, COLOR_WHITE);
    
    // Draw Line 2 Centered
    if (line2[0]) {
        int l2_len = 0; while(line2[l2_len]) l2_len++;
        int l2_w = l2_len * 8;
        draw_string(x + (80 - l2_w)/2, y + 40, line2, COLOR_WHITE);
    }
}

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
    int cup_w = size;
    int cup_h = size - 2;
    
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
    draw_rect(x + 29, y, 20, 25, COLOR_WHITE);
    draw_rect(x + 29, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 29, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 49, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 29, y + 25, 21, 1, COLOR_BLACK);
    
    // Label
    draw_icon_label(x, y, label);
}

void draw_folder_icon(int x, int y, const char *label) {
    // Folder icon (yellow folder)
    // Folder tab
    draw_rect(x + 27, y, 15, 6, COLOR_LTGRAY);
    draw_rect(x + 27, y, 15, 1, COLOR_BLACK);
    draw_rect(x + 27, y, 1, 6, COLOR_BLACK);
    draw_rect(x + 41, y, 1, 6, COLOR_BLACK);
    
    // Folder body
    draw_rect(x + 27, y + 6, 25, 15, COLOR_APPLE_YELLOW);
    draw_rect(x + 27, y + 6, 25, 1, COLOR_BLACK);
    draw_rect(x + 27, y + 6, 1, 15, COLOR_BLACK);
    draw_rect(x + 51, y + 6, 1, 15, COLOR_BLACK);
    draw_rect(x + 27, y + 20, 25, 1, COLOR_BLACK);
    
    // Label
    draw_icon_label(x, y, label);
}

void draw_document_icon(int x, int y, const char *label) {
    // Document icon (white paper with lines)
    draw_rect(x + 29, y, 20, 25, COLOR_WHITE);
    draw_rect(x + 29, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 29, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 49, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 29, y + 25, 21, 1, COLOR_BLACK);
    
    // Lines on document
    draw_rect(x + 33, y + 8, 12, 1, COLOR_BLACK);
    draw_rect(x + 33, y + 12, 12, 1, COLOR_BLACK);
    draw_rect(x + 33, y + 16, 12, 1, COLOR_BLACK);
    
    // Label
    draw_icon_label(x, y, label);
}

void draw_notepad_icon(int x, int y, const char *label) {
    // Notepad icon (Blue notebook)
    draw_rect(x + 29, y, 20, 25, COLOR_BLUE);
    draw_rect(x + 29, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 29, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 49, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 29, y + 25, 21, 1, COLOR_BLACK);
    
    // White page inside
    draw_rect(x + 31, y + 2, 17, 22, COLOR_WHITE);
    // Lines
    draw_rect(x + 33, y + 6, 13, 1, COLOR_GRAY);
    draw_rect(x + 33, y + 10, 13, 1, COLOR_GRAY);
    draw_rect(x + 33, y + 14, 13, 1, COLOR_GRAY);
    
    draw_icon_label(x, y, label);
}

void draw_calculator_icon(int x, int y, const char *label) {
    // Calculator icon
    draw_rect(x + 29, y, 20, 25, COLOR_DKGRAY);
    draw_rect(x + 29, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 29, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 49, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 29, y + 25, 21, 1, COLOR_BLACK);
    
    // Screen
    draw_rect(x + 32, y + 3, 14, 6, COLOR_APPLE_GREEN);
    
    // Buttons
    for(int r=0; r<3; r++) {
        for(int c=0; c<3; c++) {
            draw_rect(x + 32 + c*5, y + 12 + r*4, 3, 2, COLOR_WHITE);
        }
    }
    
    draw_icon_label(x, y, label);
}

void draw_terminal_icon(int x, int y, const char *label) {
    // Terminal icon
    draw_rect(x + 27, y + 2, 24, 20, COLOR_BLACK);
    draw_rect(x + 27, y + 2, 24, 1, COLOR_GRAY);
    draw_rect(x + 27, y + 2, 1, 20, COLOR_GRAY);
    draw_rect(x + 51, y + 2, 1, 20, COLOR_GRAY);
    draw_rect(x + 27, y + 22, 25, 1, COLOR_GRAY);
    
    // Prompt
    draw_rect(x + 31, y + 6, 4, 1, COLOR_APPLE_GREEN); // >
    draw_rect(x + 32, y + 7, 2, 1, COLOR_APPLE_GREEN);
    draw_rect(x + 31, y + 8, 4, 1, COLOR_APPLE_GREEN);
    draw_rect(x + 37, y + 6, 6, 1, COLOR_APPLE_GREEN); // _
    
    draw_icon_label(x, y, label);
}

void draw_minesweeper_icon(int x, int y, const char *label) {
    // Mine icon
    draw_rect(x + 29, y, 20, 25, COLOR_LTGRAY);
    draw_rect(x + 29, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 29, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 49, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 29, y + 25, 21, 1, COLOR_BLACK);
    
    // Mine
    draw_rect(x + 36, y + 8, 6, 8, COLOR_BLACK);
    draw_rect(x + 34, y + 10, 10, 4, COLOR_BLACK);
    // Spikes
    draw_rect(x + 39, y + 6, 1, 12, COLOR_BLACK);
    draw_rect(x + 33, y + 12, 12, 1, COLOR_BLACK);
    
    draw_icon_label(x, y, label);
}

void draw_control_panel_icon(int x, int y, const char *label) {
    // Control Panel (Gear/Sliders)
    draw_rect(x + 29, y, 20, 25, COLOR_GRAY);
    draw_rect(x + 29, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 29, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 49, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 29, y + 25, 21, 1, COLOR_BLACK);
    
    // Sliders
    draw_rect(x + 34, y + 5, 2, 15, COLOR_DKGRAY);
    draw_rect(x + 33, y + 10, 4, 3, COLOR_WHITE); // Knob
    
    draw_rect(x + 42, y + 5, 2, 15, COLOR_DKGRAY);
    draw_rect(x + 41, y + 16, 4, 3, COLOR_WHITE); // Knob
    
    draw_icon_label(x, y, label);
}

void draw_about_icon(int x, int y, const char *label) {
    // About icon (Info)
    draw_rect(x + 29, y, 20, 25, COLOR_WHITE);
    draw_rect(x + 29, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 29, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 49, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 29, y + 25, 21, 1, COLOR_BLACK);
    
    // 'i'
    draw_rect(x + 38, y + 5, 3, 3, COLOR_BLUE); // Dot
    draw_rect(x + 38, y + 10, 3, 10, COLOR_BLUE); // Body
    
    draw_icon_label(x, y, label);
}

void draw_recycle_bin_icon(int x, int y, const char *label) {
    // Recycle Bin (Trash can)
    draw_rect(x + 29, y, 20, 25, COLOR_LTGRAY);
    draw_rect(x + 29, y, 20, 1, COLOR_BLACK);
    draw_rect(x + 29, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 49, y, 1, 25, COLOR_BLACK);
    draw_rect(x + 29, y + 25, 21, 1, COLOR_BLACK);
    
    // Ribs
    draw_rect(x + 32, y + 5, 2, 15, COLOR_DKGRAY);
    draw_rect(x + 38, y + 5, 2, 15, COLOR_DKGRAY);
    draw_rect(x + 44, y + 5, 2, 15, COLOR_DKGRAY);
    
    draw_icon_label(x, y, label);
}

void draw_window(Window *win) {
    if (!win->visible) return;
    
    // Main Body
    draw_bevel_rect(win->x, win->y, win->w, win->h, false);
    
    // Title Bar
    uint32_t title_color = win->focused ? COLOR_RED : COLOR_DKGRAY;
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
        // Taskbar
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
    
    // Draw Desktop Icons
    for (int i = 0; i < desktop_icon_count; i++) {
        DesktopIcon *icon = &desktop_icons[i];
        if (icon->type == 1) draw_folder_icon(icon->x, icon->y, icon->name);
        else if (icon->type == 2) {
            // App icon - strip .app for display
            char label[64];
            int len = 0;
            while(icon->name[len] && len < 63) { label[len] = icon->name[len]; len++; }
            label[len] = 0;
            // Remove .app suffix if present
            if (len > 9 && str_ends_with(label, ".shortcut")) {
                label[len-9] = 0;
            }
            
            if (str_starts_with(icon->name, "Notepad")) draw_notepad_icon(icon->x, icon->y, label);
            else if (str_starts_with(icon->name, "Calculator")) draw_calculator_icon(icon->x, icon->y, label);
            else if (str_starts_with(icon->name, "Terminal")) draw_terminal_icon(icon->x, icon->y, label);
            else if (str_starts_with(icon->name, "Minesweeper")) draw_minesweeper_icon(icon->x, icon->y, label);
            else if (str_starts_with(icon->name, "Control Panel")) draw_control_panel_icon(icon->x, icon->y, label);
            else if (str_starts_with(icon->name, "About")) draw_about_icon(icon->x, icon->y, label);
            else if (str_starts_with(icon->name, "Recycle Bin")) draw_recycle_bin_icon(icon->x, icon->y, label);
            else if (str_starts_with(icon->name, "Explorer")) draw_folder_icon(icon->x, icon->y, label);
            else draw_icon(icon->x, icon->y, label);
        } else draw_document_icon(icon->x, icon->y, icon->name);
    }
    
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
    
    // 5. Start Button
    draw_bevel_rect(2, sh - 26, 90, 24, start_menu_open);
    // Draw BrewOS logo
    draw_coffee_cup(5, sh - 24, 20);
    // Draw BrewOS text
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
    
    // Desktop Context Menu
    if (desktop_menu_visible) {
        int menu_w = 140;
        int menu_h = 100; // 4 items * 25
        
        draw_rect(desktop_menu_x, desktop_menu_y, menu_w, menu_h, COLOR_LTGRAY);
        draw_bevel_rect(desktop_menu_x, desktop_menu_y, menu_w, menu_h, true);
        
        bool can_cut_copy = (desktop_menu_target_icon != -1);
        bool can_paste = explorer_clipboard_has_content();
        
        // If target is a file (not folder), paste is disabled
        if (desktop_menu_target_icon != -1) {
            DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
            if (icon->type != 1) can_paste = false; // 1 is folder
        }
        
        int item_h = 25;
        draw_string(desktop_menu_x + 5, desktop_menu_y + 5, "Cut", can_cut_copy ? COLOR_BLACK : COLOR_DKGRAY);
        draw_string(desktop_menu_x + 5, desktop_menu_y + 5 + item_h, "Copy", can_cut_copy ? COLOR_BLACK : COLOR_DKGRAY);
        draw_string(desktop_menu_x + 5, desktop_menu_y + 5 + item_h * 2, "Paste", can_paste ? COLOR_BLACK : COLOR_DKGRAY);
        draw_string(desktop_menu_x + 5, desktop_menu_y + 5 + item_h * 3, "Delete", can_cut_copy ? COLOR_RED : COLOR_DKGRAY);
    }
    
    // Message Box
    if (msg_box_visible) {
        int mw = 320;
        int mh = 100;
        int mx = (sw - mw) / 2;
        int my = (sh - mh) / 2;
        
        draw_rect(mx, my, mw, mh, COLOR_LTGRAY);
        draw_bevel_rect(mx, my, mw, mh, false);
        draw_rect(mx + 3, my + 3, mw - 6, 20, COLOR_BLUE);
        draw_string(mx + 8, my + 8, msg_box_title, COLOR_WHITE);
        draw_string(mx + 10, my + 40, msg_box_text, COLOR_BLACK);
        draw_button(mx + mw/2 - 30, my + 70, 60, 20, "OK", false);
    }
    
    // Custom Overlay (VM Graphics)
    if (wm_custom_paint_hook) {
        wm_custom_paint_hook();
    }
    
    // Draw Dragged Icon
    if (is_dragging_file) {
        // Extract filename for label
        // Just draw a generic icon at mouse pos
        if (drag_icon_type == 1) draw_folder_icon(mx - 20, my - 20, "Moving...");
        else if (drag_icon_type == 2) draw_icon(mx - 20, my - 20, "Moving...");
        else draw_document_icon(mx - 20, my - 20, "Moving...");
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
    int sw = get_screen_width();
    
    if (msg_box_visible) {
        int mw = 320;
        int mh = 100;
        int mx = (sw - mw) / 2;
        int my = (sh - mh) / 2;
        if (rect_contains(mx + mw/2 - 30, my + 70, 60, 20, x, y)) {
            msg_box_visible = false;
            force_redraw = true;
        }
        return;
    }
    
    // Handle Desktop Context Menu Click
    if (desktop_menu_visible) {
        int menu_w = 140;
        int menu_h = 100;
        if (rect_contains(desktop_menu_x, desktop_menu_y, menu_w, menu_h, x, y)) {
            int rel_y = y - desktop_menu_y;
            int item = rel_y / 25;
            
            if (item == 0 && desktop_menu_target_icon != -1) { // Cut
                DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                char path[128] = "/Desktop/";
                int p=9; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                explorer_clipboard_cut(path);
            } else if (item == 1 && desktop_menu_target_icon != -1) { // Copy
                DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                char path[128] = "/Desktop/";
                int p=9; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                explorer_clipboard_copy(path);
            } else if (item == 2) { // Paste
                bool can_paste = explorer_clipboard_has_content();
                if (desktop_menu_target_icon != -1) {
                    DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                    if (icon->type != 1) can_paste = false;
                }
                
                if (can_paste) {
                    if (desktop_menu_target_icon != -1 && desktop_icons[desktop_menu_target_icon].type == 1) {
                        // Paste into folder
                        char path[128] = "/Desktop/";
                        DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                        int p=9; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                        explorer_clipboard_paste(path);
                    } else {
                        // Paste to desktop
                        explorer_clipboard_paste("/Desktop");
                    }
                    refresh_desktop_icons();
                }
            }
            else if (item == 3 && desktop_menu_target_icon != -1) { // Delete
                DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                char path[128] = "/Desktop/";
                int p=9; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                explorer_delete_recursive(path);
                refresh_desktop_icons();
            }
        }
        desktop_menu_visible = false;
        force_redraw = true;
        return;
    }
    
    // Check Start Button
    if (rect_contains(2, sh - 26, 90, 24, x, y)) {
        start_menu_open = !start_menu_open;
        force_redraw = true;
        pending_desktop_icon_click = -1;
        return;
    }
    
    // Start Menu items handled in wm_handle_mouse (on up/drag) to support dragging shortcuts.
    
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
        pending_desktop_icon_click = -1;
    } else {
        // No window clicked - check desktop icons
        // Clear focus from all windows first
        for (int w = 0; w < window_count; w++) {
            all_windows[w]->focused = false;
        }
        
        pending_desktop_icon_click = -1;
        
        for (int i = 0; i < desktop_icon_count; i++) {
            DesktopIcon *icon = &desktop_icons[i];
            if (rect_contains(icon->x + 20, icon->y, 40, 40, x, y)) {
                // Handle click - Defer to mouse up to allow dragging
                pending_desktop_icon_click = i;
                return;
            }
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
    desktop_menu_visible = false; // Close if open
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
    } else {
        // Desktop Right Click
        desktop_menu_visible = true;
        desktop_menu_x = x;
        desktop_menu_y = y;
        desktop_menu_target_icon = -1;
        
        // Check if clicked on an icon
        for (int i = 0; i < desktop_icon_count; i++) {
            if (rect_contains(desktop_icons[i].x + 20, desktop_icons[i].y, 40, 40, x, y)) {
                desktop_menu_target_icon = i;
                break;
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
        // Mouse Down
        drag_start_x = mx;
        drag_start_y = my;
        // Check Start Menu for potential drag
        if (start_menu_open) {
            int menu_h = 230;
            int menu_y = sh - 28 - menu_h;
            if (rect_contains(0, menu_y, 120, menu_h, mx, my)) {
                if (my < menu_y + 25) start_menu_pending_app = "Explorer";
                else if (my < menu_y + 45) start_menu_pending_app = "Notepad";
                else if (my < menu_y + 65) start_menu_pending_app = "Editor";
                else if (my < menu_y + 85) start_menu_pending_app = "Terminal";
                else if (my < menu_y + 105) start_menu_pending_app = "Calculator";
                else if (my < menu_y + 125) start_menu_pending_app = "Minesweeper";
                else if (my < menu_y + 145) start_menu_pending_app = "Control Panel";
                else if (my < menu_y + 165) start_menu_pending_app = "About";
                else if (my < menu_y + 185) start_menu_pending_app = "Shutdown";
                else start_menu_pending_app = "Restart";
            } else {
                wm_handle_click(mx, my);
            }
        } else {
            wm_handle_click(mx, my);
        }
    } else if (right && !prev_right) {
        wm_handle_right_click(mx, my);
    } else if (left && is_dragging && drag_window) {
        drag_window->x = mx - drag_offset_x;
        drag_window->y = my - drag_offset_y;
        // Mark for full redraw since window moved
        force_redraw = true;
    } else if (left && !is_dragging && !is_dragging_file && (dx != 0 || dy != 0)) {
        // Check deadzone
        int dist_x = mx - drag_start_x;
        int dist_y = my - drag_start_y;
        if (dist_x < 0) dist_x = -dist_x;
        if (dist_y < 0) dist_y = -dist_y;
        
        if (dist_x >= 5 || dist_y >= 5) {
            // Check for Start Menu Drag
            if (start_menu_pending_app) {
                // Start dragging app from start menu
                is_dragging_file = true;
                drag_icon_type = 2;
                // Construct special path for app drag
                char *p = drag_file_path;
                const char *prefix = "::APP::";
                while(*prefix) *p++ = *prefix++;
                char *n = start_menu_pending_app;
                while(*n) *p++ = *n++;
                *p = 0;
                start_menu_pending_app = NULL;
            }
            
            // Mouse moving with left button, check for file drag start
            // 1. Check Desktop Icons
            if (pending_desktop_icon_click != -1) {
                int i = pending_desktop_icon_click;
                DesktopIcon *icon = &desktop_icons[i];
                is_dragging_file = true;
                drag_icon_type = icon->type;
                pending_desktop_icon_click = -1; // Cancel pending click since we are dragging
                drag_icon_orig_x = icon->x;
                drag_icon_orig_y = icon->y;
                // Construct path
                char path[128] = "/Desktop/";
                int p=9; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                int k=0; while(path[k]) { drag_file_path[k] = path[k]; k++; } drag_file_path[k]=0;
            }
            // 2. Check Explorer Items
            if (!is_dragging_file) {
                bool is_dir;
                if (explorer_get_file_at(drag_start_x, drag_start_y, drag_file_path, &is_dir)) {
                    is_dragging_file = true;
                    drag_icon_type = is_dir ? 1 : 0;
                    explorer_clear_click_state();
                }
            }
            
            if (is_dragging_file) force_redraw = true;
        }
        
    } else if (!left) {
        if (is_dragging) {
            is_dragging = false;
            drag_window = NULL;
            force_redraw = true;
        }
        
        // Handle Start Menu Click (Mouse Up without Drag)
        if (start_menu_pending_app) {
            // Launch App
            if (str_starts_with(start_menu_pending_app, "Explorer")) {
                win_explorer.visible = true; win_explorer.focused = true; explorer_reset();
            } else if (str_starts_with(start_menu_pending_app, "Notepad")) {
                win_notepad.visible = true; win_notepad.focused = true;
            } else if (str_starts_with(start_menu_pending_app, "Editor")) {
                win_editor.visible = true; win_editor.focused = true;
            } else if (str_starts_with(start_menu_pending_app, "Terminal")) {
                win_cmd.visible = true; win_cmd.focused = true; cmd_reset();
            } else if (str_starts_with(start_menu_pending_app, "Calculator")) {
                win_calculator.visible = true; win_calculator.focused = true;
            } else if (str_starts_with(start_menu_pending_app, "Minesweeper")) {
                win_minesweeper.visible = true; win_minesweeper.focused = true;
            } else if (str_starts_with(start_menu_pending_app, "Control Panel")) {
                win_control_panel.visible = true; win_control_panel.focused = true;
            } else if (str_starts_with(start_menu_pending_app, "About")) {
                win_about.visible = true; win_about.focused = true;
            } else if (str_starts_with(start_menu_pending_app, "Shutdown")) {
                cli_cmd_shutdown(NULL);
            } else if (str_starts_with(start_menu_pending_app, "Restart")) {
                cli_cmd_reboot(NULL);
            }
            
            // Bring launched window to front
            int max_z = 0;
            for (int i = 0; i < window_count; i++) {
                if (all_windows[i]->z_index > max_z) max_z = all_windows[i]->z_index;
            }
            if (win_explorer.visible && win_explorer.focused) win_explorer.z_index = max_z + 1;
            if (win_notepad.visible && win_notepad.focused) win_notepad.z_index = max_z + 1;
            if (win_editor.visible && win_editor.focused) win_editor.z_index = max_z + 1;
            if (win_cmd.visible && win_cmd.focused) win_cmd.z_index = max_z + 1;
            if (win_calculator.visible && win_calculator.focused) win_calculator.z_index = max_z + 1;
            if (win_minesweeper.visible && win_minesweeper.focused) win_minesweeper.z_index = max_z + 1;
            if (win_control_panel.visible && win_control_panel.focused) win_control_panel.z_index = max_z + 1;
            if (win_about.visible && win_about.focused) win_about.z_index = max_z + 1;
            
            start_menu_open = false;
            start_menu_pending_app = NULL;
            force_redraw = true;
        }
        
        // Handle Desktop Icon Click (Mouse Up)
        if (pending_desktop_icon_click != -1) {
            int i = pending_desktop_icon_click;
            if (i < desktop_icon_count) {
                DesktopIcon *icon = &desktop_icons[i];
                if (icon->type == 2) { // App Shortcut
                    // Check name to launch app
                    if (str_ends_with(icon->name, "Notepad.shortcut")) {
                        win_notepad.visible = true; win_notepad.focused = true;
                        notepad_reset();
                    } else if (str_ends_with(icon->name, "Calculator.shortcut")) {
                        win_calculator.visible = true; win_calculator.focused = true;
                    } else if (str_ends_with(icon->name, "Minesweeper.shortcut")) {
                        win_minesweeper.visible = true; win_minesweeper.focused = true;
                    } else if (str_ends_with(icon->name, "Control Panel.shortcut")) {
                        win_control_panel.visible = true; win_control_panel.focused = true;
                    } else if (str_ends_with(icon->name, "Terminal.shortcut")) {
                        win_cmd.visible = true; win_cmd.focused = true;
                    } else if (str_ends_with(icon->name, "About.shortcut")) {
                        win_about.visible = true; win_about.focused = true;
                    } else if (str_ends_with(icon->name, "Explorer.shortcut")) {
                        win_explorer.visible = true; win_explorer.focused = true;
                        explorer_reset();
                    } else if (str_ends_with(icon->name, "Recycle Bin.shortcut")) {
                        explorer_open_directory("/RecycleBin");
                    }
                    
                    // Generic Shortcut Handling
                    char path[128] = "/Desktop/";
                    int p=9; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                    
                    if (str_ends_with(icon->name, ".shortcut") && !str_starts_with(icon->name, "Recycle Bin")) {
                        FAT32_FileHandle *fh = fat32_open(path, "r");
                        if (fh) {
                            char buf[256];
                            int len = fat32_read(fh, buf, 255);
                            fat32_close(fh);
                            if (len > 0) {
                                buf[len] = 0;
                                if (fat32_is_directory(buf)) {
                                    explorer_open_directory(buf);
                                } else {
                                    win_editor.visible = true; win_editor.focused = true;
                                    editor_open_file(buf);
                                }
                                pending_desktop_icon_click = -1;
                                return;
                            }
                        }
                    }
                    // Bring to front
                    int max_z = 0;
                    for (int w = 0; w < window_count; w++) {
                        if (all_windows[w]->z_index > max_z) max_z = all_windows[w]->z_index;
                    }
                    if (win_notepad.visible && win_notepad.focused) win_notepad.z_index = max_z + 1;
                    if (win_calculator.visible && win_calculator.focused) win_calculator.z_index = max_z + 1;
                    if (win_minesweeper.visible && win_minesweeper.focused) win_minesweeper.z_index = max_z + 1;
                    if (win_control_panel.visible && win_control_panel.focused) win_control_panel.z_index = max_z + 1;
                    if (win_cmd.visible && win_cmd.focused) win_cmd.z_index = max_z + 1;
                    if (win_about.visible && win_about.focused) win_about.z_index = max_z + 1;
                    if (win_explorer.visible && win_explorer.focused) win_explorer.z_index = max_z + 1;
                } else if (icon->type == 1) { // Folder
                    char path[128] = "/Desktop/";
                    int p=9; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                    explorer_open_directory(path);
                    
                    int max_z = 0;
                    for (int w = 0; w < window_count; w++) {
                        if (all_windows[w]->z_index > max_z) max_z = all_windows[w]->z_index;
                    }
                    win_explorer.z_index = max_z + 1;
                } else { // File
                    win_editor.visible = true; win_editor.focused = true;
                    char path[128] = "/Desktop/";
                    int p=9; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                    editor_open_file(path);
                }
            }
            pending_desktop_icon_click = -1;
        }
        
        if (is_dragging_file) {
            // Drop logic
            
            // Check drop target
            if (win_explorer.visible && rect_contains(win_explorer.x, win_explorer.y, win_explorer.w, win_explorer.h, mx, my)) {
                // Dropped on Explorer
                // If source was desktop, we need to refresh desktop to remove the icon
                bool from_desktop = str_starts_with(drag_file_path, "/Desktop/");
                
                if (drag_file_path[0] != ':') {
                    char target_path[256];
                    bool is_dir;
                    // Check if dropped on a folder inside explorer
                    if (explorer_get_file_at(mx, my, target_path, &is_dir) && is_dir) {
                        explorer_import_file_to(drag_file_path, target_path);
                    } else {
                        // Dropped in current dir
                        explorer_import_file(drag_file_path);
                    }
                    
                }
                if (from_desktop) {
                    refresh_desktop_icons();
                }
            } else {
                // Dropped on Desktop (or elsewhere)
                if (drag_file_path[0] == ':' && drag_file_path[1] == ':' && drag_file_path[2] == 'A') {
                    // Dropped Start Menu App -> Create Shortcut
                    create_desktop_shortcut(drag_file_path + 7); // Skip ::APP::
                } else {
                    // If source was NOT desktop, move to desktop
                    // Check if path starts with /Desktop/
                    bool from_desktop = (drag_file_path[0]=='/' && drag_file_path[1]=='D' && drag_file_path[2]=='e');
                    if (!from_desktop) {
                        // Dragged from Explorer to Desktop
                        // Check limit first
                        if (desktop_icon_count >= desktop_max_cols * desktop_max_rows_per_col) {
                            wm_show_message("Error", "Desktop is full!");
                        } else {
                            explorer_import_file_to(drag_file_path, "/Desktop");
                            refresh_desktop_icons();
                        }
                        
                        // Handle insertion at specific position
                        if (desktop_auto_align && !msg_box_visible) {
                            // Find the newly added icon (it will be at the end)
                            // Extract filename from drag_file_path
                            char filename[64];
                            int len = 0; while(drag_file_path[len]) len++;
                            int s = len - 1; while(s >= 0 && drag_file_path[s] != '/') s--;
                            s++;
                            int d = 0; while(drag_file_path[s] && d < 63) filename[d++] = drag_file_path[s++];
                            filename[d] = 0;
                            
                            int new_idx = -1;
                            for(int i=0; i<desktop_icon_count; i++) {
                                if (str_eq(desktop_icons[i].name, filename) == 0) {
                                    new_idx = i;
                                    break;
                                }
                            }
                            
                            if (new_idx != -1) {
                                int target_col = (mx - 20) / 80;
                                int target_row = (my - 20) / 80;
                                if (target_col < 0) target_col = 0;
                                if (target_row < 0) target_row = 0;
                                int target_idx = target_col * desktop_max_rows_per_col + target_row;
                                if (target_idx >= desktop_icon_count) target_idx = desktop_icon_count - 1;
                                
                                // Move new_idx to target_idx
                                DesktopIcon temp = desktop_icons[new_idx];
                                // Shift down to make space
                                for (int i = new_idx; i > target_idx; i--) desktop_icons[i] = desktop_icons[i-1];
                                desktop_icons[target_idx] = temp;
                                
                                refresh_desktop_icons(); // Re-apply layout
                            }
                        }
                    } else {
                        // Moved within desktop
                        // Find which icon was dragged
                        int dragged_idx = -1;
                        for(int i=0; i<desktop_icon_count; i++) {
                            char path[128] = "/Desktop/";
                            int p=9; int n=0; while(desktop_icons[i].name[n]) path[p++] = desktop_icons[i].name[n++]; path[p]=0;
                            if (str_eq(path, drag_file_path) == 0) {
                                dragged_idx = i;
                                break;
                            }
                        }
                        
                        if (dragged_idx != -1) {
                            // Check for drop on folder (Priority over placement)
                            bool dropped_on_folder = false;
                            for (int i = 0; i < desktop_icon_count; i++) {
                                if (i == dragged_idx) continue;
                                if (desktop_icons[i].type == 1) { // Folder
                                    // Check if mouse is over this folder icon
                                    if (rect_contains(desktop_icons[i].x + 20, desktop_icons[i].y, 40, 40, mx, my)) {
                                        char target_path[256] = "/Desktop/";
                                        int p=9; int n=0; while(desktop_icons[i].name[n]) target_path[p++] = desktop_icons[i].name[n++]; target_path[p]=0;
                                        explorer_import_file_to(drag_file_path, target_path);
                                        refresh_desktop_icons();
                                        dropped_on_folder = true;
                                        break;
                                    }
                                } else if (desktop_icons[i].type == 2 && str_starts_with(desktop_icons[i].name, "Recycle Bin")) {
                                     if (rect_contains(desktop_icons[i].x + 20, desktop_icons[i].y, 40, 40, mx, my)) {
                                        // Move to Recycle Bin
                                        explorer_import_file_to(drag_file_path, "/RecycleBin");
                                        refresh_desktop_icons();
                                        dropped_on_folder = true;
                                        break;
                                     }
                                }
                            }

                            if (!dropped_on_folder && desktop_auto_align) {
                                int cell_h = 80;
                                int rel_y = my - 20;
                                if (rel_y < 0) rel_y = 0;
                                
                                int target_col = (mx - 20) / 80;
                                if (target_col < 0) target_col = 0;
                                
                                int target_row = rel_y / cell_h;
                                int row_offset = rel_y % cell_h;
                                
                                // 20% threshold logic: Only insert "before" if in the top 20%
                                if (row_offset > (int)(cell_h * 0.2)) {
                                    target_row++;
                                }
                                
                                int target_idx = target_col * desktop_max_rows_per_col + target_row;
                                if (target_idx >= desktop_icon_count) target_idx = desktop_icon_count - 1;
                                
                                // Shift items
                                DesktopIcon temp = desktop_icons[dragged_idx];
                                if (target_idx > dragged_idx) {
                                    for (int i = dragged_idx; i < target_idx; i++) desktop_icons[i] = desktop_icons[i+1];
                                } else {
                                    for (int i = dragged_idx; i > target_idx; i--) desktop_icons[i] = desktop_icons[i-1];
                                }
                                desktop_icons[target_idx] = temp;
                                refresh_desktop_icons(); // Re-applies layout
                            } else if (!dropped_on_folder) {
                                desktop_icons[dragged_idx].x = mx - 20;
                                desktop_icons[dragged_idx].y = my - 20;
                                if (desktop_snap_to_grid) {
                                    int col = (desktop_icons[dragged_idx].x - 20 + 40) / 80;
                                    int row = (desktop_icons[dragged_idx].y - 20 + 40) / 80;
                                    if (col < 0) col = 0; if (row < 0) row = 0;
                                    desktop_icons[dragged_idx].x = 20 + col * 80;
                                    desktop_icons[dragged_idx].y = 20 + row * 80;
                                }
                                
                                // Check for collision with other icons
                                // Folders already checked above, this is for overlap prevention
                                for (int i = 0; i < desktop_icon_count; i++) {
                                    if (i == dragged_idx) continue;
                                    // Simple distance check or rect overlap
                                    int dx = desktop_icons[i].x - desktop_icons[dragged_idx].x;
                                    int dy = desktop_icons[i].y - desktop_icons[dragged_idx].y;
                                    if (dx < 0) dx = -dx;
                                    if (dy < 0) dy = -dy;
                                    if (dx < 35 && dy < 35) {
                                        // Collision with non-folder (or we would have handled it)
                                        // Revert position
                                        desktop_icons[dragged_idx].x = drag_icon_orig_x;
                                        desktop_icons[dragged_idx].y = drag_icon_orig_y;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            is_dragging_file = false;
            force_redraw = true;
        }
    }
    
    if (is_dragging_file) {
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

// Input Queue
#define INPUT_QUEUE_SIZE 128
static char key_queue[INPUT_QUEUE_SIZE];
static volatile int key_head = 0;
static volatile int key_tail = 0;

static void wm_dispatch_key(char c) {
    Window *target = NULL;
    if (win_notepad.focused && win_notepad.visible) target = &win_notepad;
    else if (win_cmd.focused && win_cmd.visible) target = &win_cmd;
    else if (win_calculator.focused && win_calculator.visible) target = &win_calculator;
    else if (win_explorer.focused && win_explorer.visible) target = &win_explorer;
    else if (win_editor.focused && win_editor.visible) target = &win_editor;
    else if (win_markdown.focused && win_markdown.visible) target = &win_markdown;
    else if (win_control_panel.focused && win_control_panel.visible) target = &win_control_panel;
    
    if (!target) return;
    
    if (target->handle_key) {
        target->handle_key(target, c);
    }
    
    // Mark window as needing redraw on next timer tick
    wm_mark_dirty(target->x, target->y, target->w, target->h);
}

void wm_handle_key(char c) {
    int next = (key_head + 1) % INPUT_QUEUE_SIZE;
    if (next != key_tail) {
        key_queue[key_head] = c;
        key_head = next;
    }
}

void wm_process_input(void) {
    while (key_head != key_tail) {
        char c = key_queue[key_tail];
        key_tail = (key_tail + 1) % INPUT_QUEUE_SIZE;
        wm_dispatch_key(c);
    }
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
    markdown_init();
    control_panel_init();
    about_init();
    minesweeper_init();
    
    refresh_desktop_icons();
    
    // Initialize z-indices
    win_notepad.z_index = 0;
    win_cmd.z_index = 1;
    win_calculator.z_index = 2;
    win_explorer.z_index = 3;
    win_editor.z_index = 4;
    win_markdown.z_index = 5;
    win_control_panel.z_index = 6;
    win_about.z_index = 7;
    win_minesweeper.z_index = 8;
    
    // Register windows in array
    all_windows[0] = &win_notepad;
    all_windows[1] = &win_cmd;
    all_windows[2] = &win_calculator;
    all_windows[3] = &win_explorer;
    all_windows[4] = &win_editor;
    all_windows[5] = &win_markdown;
    all_windows[6] = &win_control_panel;
    all_windows[7] = &win_about;
    all_windows[8] = &win_minesweeper;
    window_count = 9;
    
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
    win_markdown.visible = false;
    win_control_panel.visible = false;
    win_about.visible = false;
    win_minesweeper.visible = false;
    
    force_redraw = true;
}

// Called by timer interrupt ~60Hz
void wm_timer_tick(void) {
    timer_ticks++;
    
    // Auto-refresh desktop every second (approx 60 ticks)
    // But NOT if we are currently dragging something, to avoid state conflicts
    if (!is_dragging && !is_dragging_file) {
        desktop_refresh_timer++;
        if (desktop_refresh_timer >= 60) {
            refresh_desktop_icons();
            desktop_refresh_timer = 0;
            force_redraw = true;
        }
    }
    
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
