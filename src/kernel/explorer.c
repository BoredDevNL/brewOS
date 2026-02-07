#include "explorer.h"
#include "graphics.h"
#include "fat32.h"
#include "wm.h"
#include "editor.h"
#include "markdown.h"
#include <stdbool.h>
#include <stddef.h>

// === File Explorer State ===
Window win_explorer;

#define EXPLORER_MAX_FILES 64
#define EXPLORER_ITEM_HEIGHT 80
#define EXPLORER_ITEM_WIDTH 120
#define EXPLORER_COLS 3
#define EXPLORER_ROWS 4
#define EXPLORER_PADDING 15

// Dialog states
#define DIALOG_NONE 0
#define DIALOG_CREATE_FILE 1
#define DIALOG_CREATE_FOLDER 2
#define DIALOG_DELETE_CONFIRM 3
#define DIALOG_INPUT_MAX 256

typedef struct {
    char name[256];
    bool is_directory;
    uint32_t size;
    uint32_t color;
} ExplorerItem;

typedef enum {
    ACTION_NONE,
    ACTION_CREATE_FILE,
    ACTION_CREATE_FOLDER,
    ACTION_DELETE_FILE,
    ACTION_DELETE_FOLDER
} ContextMenuAction;

static ExplorerItem items[EXPLORER_MAX_FILES];
static int item_count = 0;
static int selected_item = -1;
static char current_path[256] = "/";
static int last_clicked_item = -1;
static uint32_t last_click_time = 0;

// Dialog state
static int dialog_state = DIALOG_NONE;
static char dialog_input[DIALOG_INPUT_MAX] = "";
static int dialog_input_cursor = 0;
static char dialog_target_path[256] = "";  // For delete confirmations
static bool dialog_target_is_dir = false;  // For delete confirmations

// Dropdown menu state
static bool dropdown_menu_visible = false;
static int dropdown_menu_item_height = 25;
#define DROPDOWN_MENU_WIDTH 120
#define DROPDOWN_MENU_ITEMS 3

// File context menu state
static bool file_context_menu_visible = false;
static int file_context_menu_x = 0;
static int file_context_menu_y = 0;
static int file_context_menu_item = -1;  // Which item is being right-clicked
#define FILE_CONTEXT_MENU_WIDTH 140
#define FILE_CONTEXT_MENU_HEIGHT 50
#define FILE_CONTEXT_ITEMS 2  // "Open with Text Editor" and "Open with Markdown Viewer"

// === Helper Functions ===

static size_t explorer_strlen(const char *str);
static void explorer_strcpy(char *dest, const char *src);
static int explorer_strcmp(const char *s1, const char *s2);
static void explorer_strcat(char *dest, const char *src);
static void explorer_load_directory(const char *path);
static void explorer_handle_right_click(Window *win, int x, int y);
static void explorer_handle_file_context_menu_click(Window *win, int x, int y);

static size_t explorer_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static void explorer_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int explorer_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void explorer_strcat(char *dest, const char *src) {
    while (*dest) dest++;
    explorer_strcpy(dest, src);
}

// Get file extension (e.g., "md" from "file.md")
static const char* explorer_get_extension(const char *filename) {
    const char *dot = filename;
    const char *ext = "";
    
    // Find the last dot
    while (*dot) {
        if (*dot == '.') {
            ext = dot + 1;
        }
        dot++;
    }
    
    return ext;
}

// Check if file is markdown
static bool explorer_is_markdown_file(const char *filename) {
    const char *ext = explorer_get_extension(filename);
    return explorer_strcmp(ext, "md") == 0;
}

// === Dialog and File Operations ===

static void dialog_open_create_file(void) {
    dialog_state = DIALOG_CREATE_FILE;
    dialog_input[0] = 0;
    dialog_input_cursor = 0;
}

static void dialog_open_create_folder(void) {
    dialog_state = DIALOG_CREATE_FOLDER;
    dialog_input[0] = 0;
    dialog_input_cursor = 0;
}

static void dialog_open_delete_confirm(int item_idx) {
    if (item_idx < 0 || item_idx >= item_count) return;
    
    dialog_state = DIALOG_DELETE_CONFIRM;
    dialog_target_is_dir = items[item_idx].is_directory;
    
    // Build full path to target
    explorer_strcpy(dialog_target_path, current_path);
    if (dialog_target_path[explorer_strlen(dialog_target_path) - 1] != '/') {
        explorer_strcat(dialog_target_path, "/");
    }
    explorer_strcat(dialog_target_path, items[item_idx].name);
}

static void dialog_close(void) {
    dialog_state = DIALOG_NONE;
    dialog_input[0] = 0;
    dialog_input_cursor = 0;
    dialog_target_path[0] = 0;
}

static void dialog_confirm_create_file(void) {
    if (dialog_input[0] == 0) return;
    
    char full_path[256];
    explorer_strcpy(full_path, current_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, dialog_input);
    
    // Create empty file
    FAT32_FileHandle *file = fat32_open(full_path, "w");
    if (file) {
        fat32_close(file);
        explorer_load_directory(current_path);
    }
    
    dialog_close();
}

static void dialog_confirm_create_folder(void) {
    if (dialog_input[0] == 0) return;
    
    char full_path[256];
    explorer_strcpy(full_path, current_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, dialog_input);
    
    // Create directory
    if (fat32_mkdir(full_path)) {
        explorer_load_directory(current_path);
    }
    
    dialog_close();
}

// Recursive delete for directories
static bool explorer_delete_recursive(const char *path) {
    if (fat32_is_directory(path)) {
        // List contents and delete recursively
        FAT32_FileInfo entries[64];
        int count = fat32_list_directory(path, entries, 64);
        
        for (int i = 0; i < count; i++) {
            char child_path[256];
            explorer_strcpy(child_path, path);
            if (child_path[explorer_strlen(child_path) - 1] != '/') {
                explorer_strcat(child_path, "/");
            }
            explorer_strcat(child_path, entries[i].name);
            
            if (entries[i].is_directory) {
                explorer_delete_recursive(child_path);
            } else {
                fat32_delete(child_path);
            }
        }
        // Delete the directory itself
        return fat32_rmdir(path);
    } else {
        // Regular file
        return fat32_delete(path);
    }
}

static void dialog_confirm_delete(void) {
    explorer_delete_recursive(dialog_target_path);
    explorer_load_directory(current_path);
    dialog_close();
}

static void dropdown_menu_toggle(void) {
    dropdown_menu_visible = !dropdown_menu_visible;
}

// === Helper Functions (continued)

// === Explorer Logic ===

static uint32_t explorer_get_folder_color(const char *folder_path) {
    char color_file_path[256];
    explorer_strcpy(color_file_path, folder_path);
    if (color_file_path[explorer_strlen(color_file_path) - 1] != '/') {
        explorer_strcat(color_file_path, "/");
    }
    explorer_strcat(color_file_path, ".color");
    
    FAT32_FileHandle *file = fat32_open(color_file_path, "r");
    if (file) {
        uint32_t color = 0;
        int bytes_read = fat32_read(file, &color, sizeof(uint32_t));
        fat32_close(file);
        if (bytes_read == sizeof(uint32_t)) {
            return color;
        }
    }
    return COLOR_APPLE_YELLOW;
}

static void explorer_set_folder_color(const char *folder_path, uint32_t color) {
    char color_file_path[256];
    explorer_strcpy(color_file_path, folder_path);
    if (color_file_path[explorer_strlen(color_file_path) - 1] != '/') {
        explorer_strcat(color_file_path, "/");
    }
    explorer_strcat(color_file_path, ".color");
    
    FAT32_FileHandle *file = fat32_open(color_file_path, "w");
    if (file) {
        fat32_write(file, &color, sizeof(uint32_t));
        fat32_close(file);
    }
}

static void explorer_load_directory(const char *path) {
    explorer_strcpy(current_path, path);
    
    FAT32_FileInfo entries[EXPLORER_MAX_FILES];
    int count = fat32_list_directory(path, entries, EXPLORER_MAX_FILES);
    
    item_count = 0;
    for (int i = 0; i < count && i < EXPLORER_MAX_FILES; i++) {
        // Skip .color files
        if (explorer_strcmp(entries[i].name, ".color") == 0) {
            continue;
        }

        explorer_strcpy(items[item_count].name, entries[i].name);
        items[item_count].is_directory = entries[i].is_directory;
        items[item_count].size = entries[i].size;
        
        if (items[item_count].is_directory) {
            char subfolder_path[256];
            explorer_strcpy(subfolder_path, current_path);
            if (subfolder_path[explorer_strlen(subfolder_path) - 1] != '/') {
                explorer_strcat(subfolder_path, "/");
            }
            explorer_strcat(subfolder_path, items[item_count].name);
            items[item_count].color = explorer_get_folder_color(subfolder_path);
        } else {
            items[item_count].color = COLOR_APPLE_YELLOW;
        }
        item_count++;
    }
    
    selected_item = -1;
}

static void explorer_navigate_to(const char *dirname) {
    char new_path[256];
    
    if (explorer_strcmp(dirname, "..") == 0) {
        // Go to parent directory
        int len = explorer_strlen(current_path);
        int i = len - 1;
        
        // Skip trailing slashes
        while (i > 0 && current_path[i] == '/') i--;
        
        // Find last slash
        while (i > 0 && current_path[i] != '/') i--;
        
        if (i == 0) {
            explorer_strcpy(new_path, "/");
        } else {
            for (int j = 0; j < i; j++) {
                new_path[j] = current_path[j];
            }
            new_path[i] = 0;
        }
    } else {
        // Go to subdirectory
        explorer_strcpy(new_path, current_path);
        if (new_path[explorer_strlen(new_path) - 1] != '/') {
            explorer_strcat(new_path, "/");
        }
        explorer_strcat(new_path, dirname);
    }
    
    explorer_load_directory(new_path);
}

// Draw a simple file icon
static void explorer_draw_file_icon(int x, int y, bool is_dir, uint32_t color) {
    if (is_dir) {
        // Folder icon (colored folder) - Desktop style
        // Folder tab
        draw_rect(x + 10, y + 10, 15, 6, COLOR_LTGRAY);
        draw_rect(x + 10, y + 10, 15, 1, COLOR_BLACK);
        draw_rect(x + 10, y + 10, 1, 6, COLOR_BLACK);
        draw_rect(x + 24, y + 10, 1, 6, COLOR_BLACK);
        
        // Folder body
        draw_rect(x + 10, y + 16, 25, 15, color);
        draw_rect(x + 10, y + 16, 25, 1, COLOR_BLACK);
        draw_rect(x + 10, y + 16, 1, 15, COLOR_BLACK);
        draw_rect(x + 34, y + 16, 1, 15, COLOR_BLACK);
        draw_rect(x + 10, y + 30, 25, 1, COLOR_BLACK);
    } else {
        // Document icon - larger
        draw_rect(x + 12, y + 10, 20, 25, COLOR_WHITE);
        draw_rect(x + 12, y + 10, 20, 2, COLOR_BLACK);
        draw_rect(x + 12, y + 10, 2, 25, COLOR_BLACK);
        draw_rect(x + 30, y + 10, 2, 25, COLOR_BLACK);
        draw_rect(x + 12, y + 33, 20, 2, COLOR_BLACK);
        // Lines on document
        draw_rect(x + 15, y + 18, 14, 1, COLOR_DKGRAY);
        draw_rect(x + 15, y + 23, 14, 1, COLOR_DKGRAY);
        draw_rect(x + 15, y + 28, 14, 1, COLOR_DKGRAY);
    }
}

// === Paint Function ===

static void explorer_paint(Window *win) {
    int offset_x = win->x + 4;
    int offset_y = win->y + 24;
    
    // Fill background
    draw_rect(offset_x, offset_y, win->w - 8, win->h - 28, COLOR_LTGRAY);
    
    // Draw path bar
    int path_height = 30;
    draw_bevel_rect(offset_x + 4, offset_y + 4, win->w - 16, path_height, true);
    draw_string(offset_x + 10, offset_y + 10, "Path: ", COLOR_BLACK);
    draw_string(offset_x + 50, offset_y + 10, current_path, COLOR_BLACK);
    
    // Draw dropdown menu button (right-aligned, before back button)
    int dropdown_btn_x = win->x + win->w - 90;
    draw_button(dropdown_btn_x, offset_y + 4, 35, 30, "...", false);
    
    // Draw back button (right-aligned)
    draw_button(win->x + win->w - 40, offset_y + 4, 30, 30, "<", false);
    
    // Draw dropdown menu if visible
    if (dropdown_menu_visible) {
        int menu_x = dropdown_btn_x;
        int menu_y = offset_y + 34;
        
        // Draw menu background
        draw_rect(menu_x, menu_y, DROPDOWN_MENU_WIDTH, dropdown_menu_item_height * DROPDOWN_MENU_ITEMS, COLOR_LTGRAY);
        draw_bevel_rect(menu_x, menu_y, DROPDOWN_MENU_WIDTH, dropdown_menu_item_height * DROPDOWN_MENU_ITEMS, true);
        
        // Draw menu items
        draw_string(menu_x + 8, menu_y + 5, "New File", COLOR_BLACK);
        draw_string(menu_x + 8, menu_y + dropdown_menu_item_height + 5, "New Folder", COLOR_BLACK);
        draw_string(menu_x + 8, menu_y + dropdown_menu_item_height * 2 + 5, "Delete", COLOR_RED);
    }
    
    // Draw file list
    int content_start_y = offset_y + 40;
    
    for (int i = 0; i < item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + (row * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        // Draw item background
        uint32_t bg_color = (i == selected_item) ? COLOR_BLUE : COLOR_WHITE;
        draw_bevel_rect(item_x, item_y, EXPLORER_ITEM_WIDTH, EXPLORER_ITEM_HEIGHT, false);
        draw_rect(item_x + 2, item_y + 2, EXPLORER_ITEM_WIDTH - 4, EXPLORER_ITEM_HEIGHT - 4, bg_color);
        
        // Draw icon (larger area)
        explorer_draw_file_icon(item_x + 5, item_y + 5, items[i].is_directory, items[i].color);
        
        // Draw name below icon with text wrapping
        uint32_t text_color = (i == selected_item) ? COLOR_WHITE : COLOR_BLACK;
        int name_len = explorer_strlen(items[i].name);
        int text_x = item_x + 5;
        int text_y = item_y + 50;
        int max_name_width = EXPLORER_ITEM_WIDTH - 10;  // 110 pixels available for text
        int chars_per_line = max_name_width / 8;  // 8 pixels per character
        
        // Draw wrapped filename
        int line_offset = 0;
        char line_buffer[25];
        for (int j = 0; j < name_len; j++) {
            int pos_in_line = j % chars_per_line;
            line_buffer[pos_in_line] = items[i].name[j];
            line_buffer[pos_in_line + 1] = 0;
            
            // Draw line when we reach end of line or end of name
            if (pos_in_line == chars_per_line - 1 || j == name_len - 1) {
                draw_string(text_x, text_y + (line_offset * 10), line_buffer, text_color);
                line_offset++;
            }
        }
    }
    
    // Draw dialogs
    if (dialog_state == DIALOG_CREATE_FILE) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "Create New File", COLOR_BLACK);
        
        // Input field
        draw_bevel_rect(dlg_x + 10, dlg_y + 35, 280, 20, false);
        draw_string(dlg_x + 15, dlg_y + 40, dialog_input, COLOR_BLACK);
        draw_string(dlg_x + 15 + dialog_input_cursor * 8, dlg_y + 40, "|", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 65, 80, 25, "Create", false);
        draw_button(dlg_x + 170, dlg_y + 65, 80, 25, "Cancel", false);
    } else if (dialog_state == DIALOG_CREATE_FOLDER) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "Create New Folder", COLOR_BLACK);
        
        // Input field
        draw_bevel_rect(dlg_x + 10, dlg_y + 35, 280, 20, false);
        draw_string(dlg_x + 15, dlg_y + 40, dialog_input, COLOR_BLACK);
        draw_string(dlg_x + 15 + dialog_input_cursor * 8, dlg_y + 40, "|", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 65, 80, 25, "Create", false);
        draw_button(dlg_x + 170, dlg_y + 65, 80, 25, "Cancel", false);
    } else if (dialog_state == DIALOG_DELETE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        const char *title = dialog_target_is_dir ? "Delete Folder?" : "Delete File?";
        draw_string(dlg_x + 10, dlg_y + 10, title, COLOR_BLACK);
        
        // Message
        draw_string(dlg_x + 10, dlg_y + 35, "This action cannot be undone.", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 65, 80, 25, "Delete", false);
        draw_button(dlg_x + 170, dlg_y + 65, 80, 25, "Cancel", false);
    }
    
    // Draw file context menu if visible
    if (file_context_menu_visible && file_context_menu_item >= 0) {
        // Convert window-relative coordinates to screen coordinates for drawing
        int menu_screen_x = win->x + file_context_menu_x;
        int menu_screen_y = win->y + file_context_menu_y;
        
        if (items[file_context_menu_item].is_directory) {
            // Folder context menu (Color selection)
            int menu_height = 25 * 5; // 5 items, 25px each
            
            // Draw menu background
            draw_rect(menu_screen_x, menu_screen_y, FILE_CONTEXT_MENU_WIDTH, menu_height, COLOR_LTGRAY);
            draw_bevel_rect(menu_screen_x, menu_screen_y, FILE_CONTEXT_MENU_WIDTH, menu_height, true);
            
            // Draw menu items
            int item_h = 25;
            draw_string(menu_screen_x + 5, menu_screen_y + 5, "Blue", COLOR_APPLE_BLUE);
            draw_string(menu_screen_x + 5, menu_screen_y + item_h + 5, "Red", COLOR_RED);
            draw_string(menu_screen_x + 5, menu_screen_y + item_h * 2 + 5, "Yellow", COLOR_APPLE_YELLOW); // Text might be hard to read, but requested
            draw_string(menu_screen_x + 5, menu_screen_y + item_h * 3 + 5, "Green", COLOR_APPLE_GREEN);
            draw_string(menu_screen_x + 5, menu_screen_y + item_h * 4 + 5, "Black", COLOR_BLACK);
        } else {
            // File context menu
            // Draw menu background
            draw_rect(menu_screen_x, menu_screen_y, FILE_CONTEXT_MENU_WIDTH, FILE_CONTEXT_MENU_HEIGHT, COLOR_LTGRAY);
            draw_bevel_rect(menu_screen_x, menu_screen_y, FILE_CONTEXT_MENU_WIDTH, FILE_CONTEXT_MENU_HEIGHT, true);
            
            // Draw menu items
            int item_height = FILE_CONTEXT_MENU_HEIGHT / FILE_CONTEXT_ITEMS;
            
            // Item 1: "Open with Text Editor"
            draw_string(menu_screen_x + 5, menu_screen_y + 5, "Open w/ Editor", COLOR_BLACK);
            
            // Item 2: "Open with Markdown Viewer" (only show if file is .md)
            if (explorer_is_markdown_file(items[file_context_menu_item].name)) {
                draw_string(menu_screen_x + 5, menu_screen_y + item_height + 5, "Open w/ Markdown", COLOR_BLACK);
            }
        }
    }
}

// === Mouse Handler ===

static void explorer_handle_click(Window *win, int x, int y) {
    // Handle file context menu clicks first
    if (file_context_menu_visible) {
        explorer_handle_file_context_menu_click(win, x, y);
        return;
    }
    
    // Handle dialog clicks
    if (dialog_state == DIALOG_CREATE_FILE || dialog_state == DIALOG_CREATE_FOLDER) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        // Create button
        if (x >= dlg_x + 50 && x < dlg_x + 130 &&
            y >= dlg_y + 65 && y < dlg_y + 90) {
            if (dialog_state == DIALOG_CREATE_FILE) {
                dialog_confirm_create_file();
            } else {
                dialog_confirm_create_folder();
            }
            return;
        }
        
        // Cancel button
        if (x >= dlg_x + 170 && x < dlg_x + 250 &&
            y >= dlg_y + 65 && y < dlg_y + 90) {
            dialog_close();
            return;
        }
        
        // Input field click
        if (x >= dlg_x + 10 && x < dlg_x + 290 &&
            y >= dlg_y + 35 && y < dlg_y + 55) {
            dialog_input_cursor = (x - dlg_x - 15) / 8;
            if (dialog_input_cursor > (int)explorer_strlen(dialog_input)) {
                dialog_input_cursor = explorer_strlen(dialog_input);
            }
            return;
        }
    } else if (dialog_state == DIALOG_DELETE_CONFIRM) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        // Delete button
        if (x >= dlg_x + 50 && x < dlg_x + 130 &&
            y >= dlg_y + 65 && y < dlg_y + 90) {
            dialog_confirm_delete();
            return;
        }
        
        // Cancel button
        if (x >= dlg_x + 170 && x < dlg_x + 250 &&
            y >= dlg_y + 65 && y < dlg_y + 90) {
            dialog_close();
            return;
        }
    }
    
    // Handle dropdown menu clicks
    if (dropdown_menu_visible) {
        int dropdown_btn_x = win->w - 90;  // Window-relative
        int menu_y = 58;  // Window-relative (offset_y + 34, where offset_y = 24)
        
        // New File
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y && y < menu_y + dropdown_menu_item_height) {
            dropdown_menu_toggle();
            dialog_open_create_file();
            return;
        }
        
        // New Folder
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y + dropdown_menu_item_height && 
            y < menu_y + dropdown_menu_item_height * 2) {
            dropdown_menu_toggle();
            dialog_open_create_folder();
            return;
        }
        
        // Delete
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y + dropdown_menu_item_height * 2 && 
            y < menu_y + dropdown_menu_item_height * 3) {
            dropdown_menu_toggle();
            if (selected_item >= 0) {
                dialog_open_delete_confirm(selected_item);
            }
            return;
        }
        
        // Click outside menu closes it
        dropdown_menu_toggle();
        return;
    }
    
    // x, y are already relative to window (0,0 is top-left of window content area)
    // Check dropdown menu button
    int button_y = 28;  // Position from top of window title bar
    if (x >= win->w - 90 && x < win->w - 55 &&
        y >= button_y && y < button_y + 30) {
        // Dropdown menu button clicked
        dropdown_menu_toggle();
        return;
    }
    
    // Check back button (right-aligned)
    if (x >= win->w - 40 && x < win->w - 10 &&
        y >= button_y && y < button_y + 30) {
        // Back button clicked
        explorer_navigate_to("..");
        return;
    }
    
    // File items start at y=64 relative to window
    int content_start_y = 64;
    int offset_x = 4;
    
    for (int i = 0; i < item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + (row * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        if (x >= item_x && x < item_x + EXPLORER_ITEM_WIDTH &&
            y >= item_y && y < item_y + EXPLORER_ITEM_HEIGHT) {
            
            // Check for double-click
            if (last_clicked_item == i) {
                // Double-click detected
                if (items[i].is_directory) {
                    explorer_navigate_to(items[i].name);
                } else {
                    // Open file - check type
                    char full_path[256];
                    explorer_strcpy(full_path, current_path);
                    if (full_path[explorer_strlen(full_path) - 1] != '/') {
                        explorer_strcat(full_path, "/");
                    }
                    explorer_strcat(full_path, items[i].name);
                    
                    // Check if markdown file
                    if (explorer_is_markdown_file(items[i].name)) {
                        // Open with markdown viewer
                        win_markdown.visible = true;
                        win_markdown.focused = true;
                        int max_z = 0;
                        if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
                        if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
                        if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
                        if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
                        if (win_editor.z_index > max_z) max_z = win_editor.z_index;
                        win_markdown.z_index = max_z + 1;
                        markdown_open_file(full_path);
                    } else {
                        // Open with text editor
                        win_editor.visible = true;
                        win_editor.focused = true;
                        int max_z = 0;
                        if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
                        if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
                        if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
                        if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
                        if (win_markdown.z_index > max_z) max_z = win_markdown.z_index;
                        win_editor.z_index = max_z + 1;
                        editor_open_file(full_path);
                    }
                }
                last_clicked_item = -1;
            } else {
                // Single-click - select
                selected_item = i;
                last_clicked_item = i;
                last_click_time = 0;  // Reset for next click
            }
            return;
        }
    }
}

// === Key Handler ===

static void explorer_handle_key(Window *win, char c) {
    (void)win;
    
    // Handle dialog input
    if (dialog_state == DIALOG_CREATE_FILE || dialog_state == DIALOG_CREATE_FOLDER) {
        if (c == 27) {  // ESC - close dialog
            dialog_close();
            return;
        } else if (c == '\n') {  // ENTER - confirm
            if (dialog_state == DIALOG_CREATE_FILE) {
                dialog_confirm_create_file();
            } else {
                dialog_confirm_create_folder();
            }
            return;
        } else if (c == 8 || c == 127) {  // BACKSPACE
            if (dialog_input_cursor > 0) {
                dialog_input_cursor--;
                // Shift characters
                for (int i = dialog_input_cursor; i < (int)explorer_strlen(dialog_input); i++) {
                    dialog_input[i] = dialog_input[i + 1];
                }
            }
            return;
        } else if (c >= 32 && c < 127) {  // Printable character
            int len = explorer_strlen(dialog_input);
            if (len < DIALOG_INPUT_MAX - 1) {
                // Shift characters to make room
                for (int i = len; i >= dialog_input_cursor; i--) {
                    dialog_input[i + 1] = dialog_input[i];
                }
                dialog_input[dialog_input_cursor] = c;
                dialog_input_cursor++;
            }
            return;
        }
        return;
    }
    
    if (dialog_state == DIALOG_DELETE_CONFIRM) {
        if (c == 27) {  // ESC
            dialog_close();
            return;
        }
        return;
    }
    
    if (c == 'q' || c == 'Q') {
        win->visible = false;
        return;
    }
    
    // Close dropdown menu if open with ESC
    if (dropdown_menu_visible && c == 27) {
        dropdown_menu_toggle();
        return;
    }
    
    if (c == 17) {  // UP
        if (selected_item > 0) {
            selected_item -= EXPLORER_COLS;
            if (selected_item < 0) selected_item = 0;
        }
    } else if (c == 18) {  // DOWN
        if (selected_item < item_count - 1) {
            selected_item += EXPLORER_COLS;
            if (selected_item >= item_count) selected_item = item_count - 1;
        }
    } else if (c == 19) {  // LEFT
        if (selected_item > 0) {
            selected_item--;
        }
    } else if (c == 20) {  // RIGHT
        if (selected_item < item_count - 1) {
            selected_item++;
        }
    } else if (c == '\n') {  // ENTER
        if (selected_item >= 0 && selected_item < item_count) {
            if (items[selected_item].is_directory) {
                explorer_navigate_to(items[selected_item].name);
            }
        }
    } else if (c == 'd' || c == 'D') {  // Delete key
        if (selected_item >= 0) {
            dialog_open_delete_confirm(selected_item);
        }
    } else if (c == 'n' || c == 'N') {  // New file
        dialog_open_create_file();
    } else if (c == 'f' || c == 'F') {  // New folder
        dialog_open_create_folder();
    }
}

// === Right-Click Handler ===

static void explorer_handle_right_click(Window *win, int x, int y) {
    // File items start at y=64 relative to window
    int content_start_y = 64;
    int offset_x = 4;
    
    for (int i = 0; i < item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + (row * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        if (x >= item_x && x < item_x + EXPLORER_ITEM_WIDTH &&
            y >= item_y && y < item_y + EXPLORER_ITEM_HEIGHT) {
            
            // Right-click on a file or folder item
            // Show context menu
            file_context_menu_visible = true;
            file_context_menu_item = i;
            file_context_menu_x = x;
            file_context_menu_y = y;
            return;
        }
    }
    
    // Close menu if clicking elsewhere
    file_context_menu_visible = false;
    file_context_menu_item = -1;
}

static void explorer_handle_file_context_menu_click(Window *win, int x, int y) {
    (void)win;  // Suppress unused warning - we use absolute coordinates instead
    
    if (!file_context_menu_visible || file_context_menu_item < 0) {
        return;
    }
    
    // Adjust coordinates to be relative to context menu
    int relative_x = x - file_context_menu_x;
    int relative_y = y - file_context_menu_y;
    
    int menu_height;
    if (items[file_context_menu_item].is_directory) {
        menu_height = 25 * 5;
    } else {
        menu_height = FILE_CONTEXT_MENU_HEIGHT;
    }
    
    if (relative_x < 0 || relative_x > FILE_CONTEXT_MENU_WIDTH ||
        relative_y < 0 || relative_y > menu_height) {
        // Clicked outside menu - close it
        file_context_menu_visible = false;
        file_context_menu_item = -1;
        return;
    }
    
    if (items[file_context_menu_item].is_directory) {
        int clicked_item = relative_y / 25;
        uint32_t new_color = items[file_context_menu_item].color;
        
        if (clicked_item == 0) new_color = COLOR_APPLE_BLUE;
        else if (clicked_item == 1) new_color = COLOR_RED;
        else if (clicked_item == 2) new_color = COLOR_APPLE_YELLOW;
        else if (clicked_item == 3) new_color = COLOR_APPLE_GREEN;
        else if (clicked_item == 4) new_color = COLOR_BLACK;
        
        items[file_context_menu_item].color = new_color;
        
        // Save to file
        char full_path[256];
        explorer_strcpy(full_path, current_path);
        if (full_path[explorer_strlen(full_path) - 1] != '/') {
            explorer_strcat(full_path, "/");
        }
        explorer_strcat(full_path, items[file_context_menu_item].name);
        explorer_set_folder_color(full_path, new_color);
    } else {
        int item_height = FILE_CONTEXT_MENU_HEIGHT / FILE_CONTEXT_ITEMS;
        int clicked_item = relative_y / item_height;
        
        // Build full path
        char full_path[256];
        explorer_strcpy(full_path, current_path);
        if (full_path[explorer_strlen(full_path) - 1] != '/') {
            explorer_strcat(full_path, "/");
        }
        explorer_strcat(full_path, items[file_context_menu_item].name);
        
        if (clicked_item == 0) {
            // "Open with Text Editor"
            win_editor.visible = true;
            win_editor.focused = true;
            int max_z = 0;
            if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
            if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
            if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
            if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
            if (win_markdown.z_index > max_z) max_z = win_markdown.z_index;
            win_editor.z_index = max_z + 1;
            editor_open_file(full_path);
        } else if (clicked_item == 1 && explorer_is_markdown_file(items[file_context_menu_item].name)) {
            // "Open with Markdown Viewer"
            win_markdown.visible = true;
            win_markdown.focused = true;
            int max_z = 0;
            if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
            if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
            if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
            if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
            if (win_editor.z_index > max_z) max_z = win_editor.z_index;
            win_markdown.z_index = max_z + 1;
            markdown_open_file(full_path);
        }
    }
    
    file_context_menu_visible = false;
    file_context_menu_item = -1;
}

// === Initialization ===

void explorer_init(void) {
    win_explorer.title = "File Explorer";
    win_explorer.x = 300;
    win_explorer.y = 100;
    win_explorer.w = 600;
    win_explorer.h = 400;
    win_explorer.visible = false;
    win_explorer.focused = false;
    win_explorer.z_index = 0;
    win_explorer.paint = explorer_paint;
    win_explorer.handle_key = explorer_handle_key;
    win_explorer.handle_click = explorer_handle_click;
    win_explorer.handle_right_click = explorer_handle_right_click;
    
    explorer_load_directory("/");
}
void explorer_reset(void) {
    // Reset explorer to root directory on close/reopen
    explorer_load_directory("/");
    win_explorer.focused = false;
}