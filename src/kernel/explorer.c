#include "explorer.h"
#include "graphics.h"
#include "fat32.h"
#include "wm.h"
#include "editor.h"
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

// === Helper Functions ===

static size_t explorer_strlen(const char *str);
static void explorer_strcpy(char *dest, const char *src);
static int explorer_strcmp(const char *s1, const char *s2);
static void explorer_strcat(char *dest, const char *src);
static void explorer_load_directory(const char *path);

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

static void explorer_load_directory(const char *path) {
    explorer_strcpy(current_path, path);
    
    FAT32_FileInfo entries[EXPLORER_MAX_FILES];
    int count = fat32_list_directory(path, entries, EXPLORER_MAX_FILES);
    
    item_count = 0;
    for (int i = 0; i < count && i < EXPLORER_MAX_FILES; i++) {
        explorer_strcpy(items[i].name, entries[i].name);
        items[i].is_directory = entries[i].is_directory;
        items[i].size = entries[i].size;
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
static void explorer_draw_file_icon(int x, int y, bool is_dir) {
    if (is_dir) {
        // Folder icon - larger
        draw_rect(x + 10, y + 10, 30, 5, COLOR_BLUE);  // Tab
        draw_rect(x + 10, y + 15, 30, 25, COLOR_WHITE); // Main folder
        draw_rect(x + 10, y + 15, 2, 25, COLOR_BLACK);
        draw_rect(x + 10, y + 15, 30, 2, COLOR_BLACK);
        draw_rect(x + 38, y + 15, 2, 25, COLOR_BLACK);
        draw_rect(x + 10, y + 38, 30, 2, COLOR_BLACK);
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
        explorer_draw_file_icon(item_x + 5, item_y + 5, items[i].is_directory);
        
        // Draw name below icon
        uint32_t text_color = (i == selected_item) ? COLOR_WHITE : COLOR_BLACK;
        int name_len = explorer_strlen(items[i].name);
        int text_x = item_x + 5;
        int text_y = item_y + 50;
        
        // Truncate name if too long
        char display_name[24];
        int copy_len = name_len > 18 ? 18 : name_len;
        for (int j = 0; j < copy_len; j++) {
            display_name[j] = items[i].name[j];
        }
        display_name[copy_len] = 0;
        
        draw_string(text_x, text_y, display_name, text_color);
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
}

// === Mouse Handler ===

static void explorer_handle_click(Window *win, int x, int y) {
    // Handle dialog clicks first
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
                    // Open file in editor
                    char full_path[256];
                    explorer_strcpy(full_path, current_path);
                    if (full_path[explorer_strlen(full_path) - 1] != '/') {
                        explorer_strcat(full_path, "/");
                    }
                    explorer_strcat(full_path, items[i].name);
                    
                    // Open in editor and bring to front
                    win_editor.visible = true;
                    win_editor.focused = true;
                    int max_z = 0;
                    for (int j = 0; j < 5; j++) {  // window_count is 5
                        // Need to find max z_index - check all windows
                        if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
                        if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
                        if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
                        if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
                    }
                    win_editor.z_index = max_z + 1;
                    editor_open_file(full_path);
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
    win_explorer.handle_right_click = NULL;
    
    explorer_load_directory("/");
}
void explorer_reset(void) {
    // Reset explorer to root directory on close/reopen
    explorer_load_directory("/");
    win_explorer.focused = false;
}