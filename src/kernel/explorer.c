#include "explorer.h"
#include "graphics.h"
#include "fat32.h"
#include "wm.h"
#include "memory_manager.h"
#include "editor.h"
#include "markdown.h"
#include "cmd.h"
#include "notepad.h"
#include "calculator.h"
#include "minesweeper.h"
#include "control_panel.h"
#include "about.h"
#include <stdbool.h>
#include <stddef.h>

// === File Explorer State ===
Window win_explorer;

#define EXPLORER_MAX_FILES 64
#define EXPLORER_ITEM_HEIGHT 80
#define EXPLORER_ITEM_WIDTH 120
#define EXPLORER_COLS 4
#define EXPLORER_ROWS 4
#define EXPLORER_PADDING 15

// Dialog states
#define DIALOG_NONE 0
#define DIALOG_CREATE_FILE 1
#define DIALOG_CREATE_FOLDER 2
#define DIALOG_DELETE_CONFIRM 3
#define DIALOG_REPLACE_CONFIRM 4
#define DIALOG_REPLACE_MOVE_CONFIRM 5
#define DIALOG_CREATE_REPLACE_CONFIRM 6
#define DIALOG_INPUT_MAX 256
#define ACTION_RESTORE 108
#define DIALOG_ERROR 7
#define ACTION_CREATE_SHORTCUT 107

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
static int explorer_scroll_row = 0;

// Dialog state
static int dialog_state = DIALOG_NONE;
static char dialog_input[DIALOG_INPUT_MAX] = "";
static int dialog_input_cursor = 0;
static char dialog_target_path[256] = "";  // For delete confirmations
static bool dialog_target_is_dir = false;  // For delete confirmations
static char dialog_dest_dir[256] = "";     // For replace confirmations
static char dialog_creation_path[256] = ""; // For new file/folder creation
static char dialog_move_src[256] = "";      // For drag-drop replace

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
#define CONTEXT_MENU_ITEM_HEIGHT 25

// Clipboard state
static char clipboard_path[256] = "";
static int clipboard_action = 0; // 0=None, 1=Copy, 2=Cut
#define FILE_CONTEXT_ITEMS 2  // "Open with Text Editor" and "Open with Markdown Viewer"

typedef struct {
    const char *label;
    int action_id; // 100+ for actions
    bool enabled;
    uint32_t color;
} ExplorerContextItem;

// === Helper Functions ===

static size_t explorer_strlen(const char *str);
static void explorer_strcpy(char *dest, const char *src);
static int explorer_strcmp(const char *s1, const char *s2);
static void explorer_strcat(char *dest, const char *src);
static void explorer_load_directory(const char *path);
static void explorer_handle_right_click(Window *win, int x, int y);
static void explorer_handle_file_context_menu_click(Window *win, int x, int y);
static void explorer_perform_paste(const char *dest_dir);
static void explorer_perform_move_internal(const char *source_path, const char *dest_dir);
static void explorer_copy_recursive(const char *src_path, const char *dest_path);

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

// Helper to check if string starts with prefix
static bool explorer_str_starts_with(const char *str, const char *prefix) {
    while(*prefix) {
        if (*prefix++ != *str++) return false;
    }
    return true;
}

// Helper to check if string ends with suffix
static bool explorer_str_ends_with(const char *str, const char *suffix) {
    int str_len = explorer_strlen(str);
    int suf_len = explorer_strlen(suffix);
    if (suf_len > str_len) return false;
    return explorer_strcmp(str + str_len - suf_len, suffix) == 0;
}

// Helper for label drawing (adapted from wm.c)
static void explorer_draw_icon_label(int x, int y, const char *label) {
    char line1[10] = {0};
    char line2[10] = {0};
    int len = 0; while(label[len]) len++;
    
    if (len <= 8) {
        int i=0; while(i<len) { line1[i] = label[i]; i++; }
        line1[i] = 0;
    } else {
        int split = 8;
        int best_split = -1;
        for (int i = 7; i >= 1; i--) {
            if (label[i] == ' ' || label[i] == '.') {
                best_split = i;
                break;
            }
        }
        
        if (best_split != -1) split = best_split;
        
        int i;
        for (i = 0; i < split; i++) line1[i] = label[i];
        line1[i] = 0;
        
        int start2 = split;
        if (label[split] == ' ') start2++;
        
        int j = 0;
        while (label[start2 + j] && j < 8) {
            line2[j] = label[start2 + j];
            j++;
        }
        line2[j] = 0;
        
        if (label[start2 + j] != 0) {
            if (j > 6) { line2[6] = '.'; line2[7] = '.'; line2[8] = 0; }
            else { line2[j++] = '.'; line2[j++] = '.'; line2[j] = 0; }
        }
    }
    
    // Center in EXPLORER_ITEM_WIDTH
    int l1_len = 0; while(line1[l1_len]) l1_len++;
    int l1_w = l1_len * 8;
    draw_string(x + (EXPLORER_ITEM_WIDTH - l1_w)/2, y + 50, line1, COLOR_BLACK);
    
    if (line2[0]) {
        int l2_len = 0; while(line2[l2_len]) l2_len++;
        int l2_w = l2_len * 8;
        draw_string(x + (EXPLORER_ITEM_WIDTH - l2_w)/2, y + 60, line2, COLOR_BLACK);
    }
}

// === Dialog and File Operations ===

static bool check_desktop_limit_explorer(void) {
    if (explorer_str_starts_with(current_path, "/Desktop")) {
        // Check if root desktop
        if (explorer_strcmp(current_path, "/Desktop") == 0 || explorer_strcmp(current_path, "/Desktop/") == 0) {
             if (item_count >= desktop_max_cols * desktop_max_rows_per_col) {
                 dialog_state = DIALOG_ERROR;
                 explorer_strcpy(dialog_input, "Desktop is full!");
                 return false;
             }
        }
    }
    return true;
}

static void dialog_open_create_file(const char *path) {
    dialog_state = DIALOG_CREATE_FILE;
    dialog_input[0] = 0;
    dialog_input_cursor = 0;
    explorer_strcpy(dialog_creation_path, path);
}

static void dialog_open_create_folder(const char *path) {
    dialog_state = DIALOG_CREATE_FOLDER;
    dialog_input[0] = 0;
    dialog_input_cursor = 0;
    explorer_strcpy(dialog_creation_path, path);
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
    
    if (!check_desktop_limit_explorer()) return;
    
    char full_path[256];
    explorer_strcpy(full_path, dialog_creation_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, dialog_input);
    
    if (fat32_exists(full_path)) {
        dialog_state = DIALOG_CREATE_REPLACE_CONFIRM;
        return;
    }
    
    // Create empty file
    FAT32_FileHandle *file = fat32_open(full_path, "w");
    if (file) {
        fat32_close(file);
        explorer_load_directory(current_path);
    }
    
    dialog_close();
}

static void dialog_force_create_file(void) {
    char full_path[256];
    explorer_strcpy(full_path, dialog_creation_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, dialog_input);
    
    FAT32_FileHandle *file = fat32_open(full_path, "w");
    if (file) {
        fat32_close(file);
        explorer_load_directory(current_path);
    }
    dialog_close();
}

static void dialog_confirm_create_folder(void) {
    if (dialog_input[0] == 0) return;
    
    if (!check_desktop_limit_explorer()) return;
    
    char full_path[256];
    explorer_strcpy(full_path, dialog_creation_path);
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
bool explorer_delete_permanently(const char *path) {
    if (fat32_is_directory(path)) {
        // List contents and delete recursively
        FAT32_FileInfo *entries = (FAT32_FileInfo*)kmalloc(64 * sizeof(FAT32_FileInfo));
        if (!entries) return false;

        int count = fat32_list_directory(path, entries, 64);
        
        for (int i = 0; i < count; i++) {
            if (explorer_strcmp(entries[i].name, ".") == 0 || explorer_strcmp(entries[i].name, "..") == 0) continue;

            char child_path[256];
            explorer_strcpy(child_path, path);
            if (child_path[explorer_strlen(child_path) - 1] != '/') {
                explorer_strcat(child_path, "/");
            }
            explorer_strcat(child_path, entries[i].name);
            
            if (entries[i].is_directory) {
                explorer_delete_permanently(child_path);
            } else {
                fat32_delete(child_path);
            }
        }
        kfree(entries);
        // Delete the directory itself
        return fat32_rmdir(path);
    } else {
        // Regular file
        return fat32_delete(path);
    }
}

bool explorer_delete_recursive(const char *path) {
    if (explorer_str_starts_with(path, "/RecycleBin")) {
        return explorer_delete_permanently(path);
    } else {
        // Move to Recycle Bin
        char filename[256];
        int len = explorer_strlen(path);
        int i = len - 1;
        while (i >= 0 && path[i] != '/') i--;
        int j = 0;
        for (int k = i + 1; k < len; k++) filename[j++] = path[k];
        filename[j] = 0;
        
        char dest_path[256];
        explorer_strcpy(dest_path, "/RecycleBin/");
        explorer_strcat(dest_path, filename);
        
        // Save origin
        char origin_path[256];
        explorer_strcpy(origin_path, dest_path);
        explorer_strcat(origin_path, ".origin");
        FAT32_FileHandle *fh = fat32_open(origin_path, "w");
        if (fh) {
            fat32_write(fh, path, explorer_strlen(path));
            fat32_close(fh);
        }
        
        // Use copy + delete (permanent) to simulate move
        explorer_copy_recursive(path, dest_path);
        explorer_delete_permanently(path);
        return true;
    }
}

static void dialog_confirm_delete(void) {
    explorer_delete_recursive(dialog_target_path);
    explorer_load_directory(current_path);
    dialog_close();
}

static void dialog_confirm_replace(void) {
    explorer_perform_paste(dialog_dest_dir);
    dialog_close();
}

static void dialog_confirm_replace_move(void) {
    explorer_perform_move_internal(dialog_move_src, dialog_dest_dir);
    dialog_close();
}

// === Clipboard Functions ===

void explorer_clipboard_copy(const char *path) {
    explorer_strcpy(clipboard_path, path);
    clipboard_action = 1; // Copy
}

void explorer_clipboard_cut(const char *path) {
    explorer_strcpy(clipboard_path, path);
    clipboard_action = 2; // Cut
}

bool explorer_clipboard_has_content(void) {
    return clipboard_action != 0 && clipboard_path[0] != 0;
}

static void explorer_copy_recursive(const char *src_path, const char *dest_path) {
    if (fat32_is_directory(src_path)) {
        fat32_mkdir(dest_path);
        FAT32_FileInfo *files = (FAT32_FileInfo*)kmalloc(64 * sizeof(FAT32_FileInfo));
        if (!files) return;
        
        int count = fat32_list_directory(src_path, files, 64);
        for (int i = 0; i < count; i++) {
            if (explorer_strcmp(files[i].name, ".") == 0 || explorer_strcmp(files[i].name, "..") == 0) continue;
            
            char s_sub[256], d_sub[256];
            explorer_strcpy(s_sub, src_path);
            if (s_sub[explorer_strlen(s_sub)-1] != '/') explorer_strcat(s_sub, "/");
            explorer_strcat(s_sub, files[i].name);
            
            explorer_strcpy(d_sub, dest_path);
            if (d_sub[explorer_strlen(d_sub)-1] != '/') explorer_strcat(d_sub, "/");
            explorer_strcat(d_sub, files[i].name);
            
            explorer_copy_recursive(s_sub, d_sub);
        }
        kfree(files);
    } else {
        // Copy file
        FAT32_FileHandle *src = fat32_open(src_path, "r");
        FAT32_FileHandle *dst = fat32_open(dest_path, "w");
        if (src && dst) {
            uint8_t *buf = (uint8_t*)kmalloc(4096);
            if (buf) {
                int bytes;
                while ((bytes = fat32_read(src, buf, 4096)) > 0) fat32_write(dst, buf, bytes);
                kfree(buf);
            }
        }
        if (src) fat32_close(src);
        if (dst) fat32_close(dst);
    }
}

static void explorer_copy_file_internal(const char *src_path, const char *dest_dir) {
    char filename[256];
    int len = explorer_strlen(src_path);
    int i = len - 1;
    while (i >= 0 && src_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = src_path[k];
    filename[j] = 0;
    
    char dest_path[256];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    if (explorer_strcmp(src_path, dest_path) == 0) return;
    
    explorer_copy_recursive(src_path, dest_path);
}

static void explorer_perform_paste(const char *dest_dir) {
    explorer_copy_file_internal(clipboard_path, dest_dir);
    
    if (clipboard_action == 2) { // Cut
        // Delete source
        if (fat32_is_directory(clipboard_path)) {
            explorer_delete_permanently(clipboard_path);
        } else {
            fat32_delete(clipboard_path);
        }
        clipboard_action = 0; // Clear clipboard after cut-paste
    }
    explorer_refresh();
}

void explorer_clipboard_paste(const char *dest_dir) {
    if (!explorer_clipboard_has_content()) return;
    
    // Check for collision
    char filename[256];
    int len = explorer_strlen(clipboard_path);
    int i = len - 1;
    while (i >= 0 && clipboard_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = clipboard_path[k];
    filename[j] = 0;
    
    char dest_path[256];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    if (fat32_exists(dest_path)) {
        dialog_state = DIALOG_REPLACE_CONFIRM;
        explorer_strcpy(dialog_dest_dir, dest_dir);
        return;
    }
    
    explorer_perform_paste(dest_dir);
}

void explorer_create_shortcut(const char *target_path) {
    char filename[256];
    int len = explorer_strlen(target_path);
    int i = len - 1;
    while (i >= 0 && target_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = target_path[k];
    filename[j] = 0;
    
    char shortcut_path[256];
    explorer_strcpy(shortcut_path, current_path);
    if (shortcut_path[explorer_strlen(shortcut_path)-1] != '/') explorer_strcat(shortcut_path, "/");
    explorer_strcat(shortcut_path, filename);
    explorer_strcat(shortcut_path, ".shortcut");
    
    FAT32_FileHandle *fh = fat32_open(shortcut_path, "w");
    if (fh) {
        fat32_write(fh, target_path, explorer_strlen(target_path));
        fat32_close(fh);
        explorer_load_directory(current_path);
    }
}

static void dropdown_menu_toggle(void) {
    dropdown_menu_visible = !dropdown_menu_visible;
}

// === Context Menu Builder ===
static int explorer_build_context_menu(ExplorerContextItem *items_out) {
    int count = 0;
    if (file_context_menu_item == -1) {
        if (explorer_str_starts_with(current_path, "/RecycleBin")) {
            // Dead space in Recycle Bin - no actions for now
            return 0;
        }
        // Dead space
        items_out[count++] = (ExplorerContextItem){"New File", 101, true, COLOR_BLACK};
        items_out[count++] = (ExplorerContextItem){"New Folder", 102, true, COLOR_BLACK};
        items_out[count++] = (ExplorerContextItem){"Paste", 103, explorer_clipboard_has_content(), explorer_clipboard_has_content() ? COLOR_BLACK : COLOR_DKGRAY};
    } else {
        if (explorer_str_starts_with(current_path, "/RecycleBin")) {
            items_out[count++] = (ExplorerContextItem){"Restore", ACTION_RESTORE, true, COLOR_BLACK};
            items_out[count++] = (ExplorerContextItem){"Delete Forever", 106, true, COLOR_RED};
            return count;
        }

        bool is_dir = items[file_context_menu_item].is_directory;
        
        if (!is_dir) {
             items_out[count++] = (ExplorerContextItem){"Open", 100, true, COLOR_BLACK};
             items_out[count++] = (ExplorerContextItem){"Open w/ textedit", 110, true, COLOR_BLACK};
             if (explorer_is_markdown_file(items[file_context_menu_item].name)) {
                 items_out[count++] = (ExplorerContextItem){"Open w/ Markdown", 109, true, COLOR_BLACK};
             }
        }
        
        items_out[count++] = (ExplorerContextItem){"Cut", 104, true, COLOR_BLACK};
        items_out[count++] = (ExplorerContextItem){"Copy", 105, true, COLOR_BLACK};
        
        if (is_dir) {
            items_out[count++] = (ExplorerContextItem){"Paste", 103, explorer_clipboard_has_content(), explorer_clipboard_has_content() ? COLOR_BLACK : COLOR_DKGRAY};
        }
        
        items_out[count++] = (ExplorerContextItem){"Delete", 106, true, COLOR_RED};
        items_out[count++] = (ExplorerContextItem){"Create Shortcut", ACTION_CREATE_SHORTCUT, true, COLOR_BLACK};
        
        if (is_dir) {
            items_out[count++] = (ExplorerContextItem){"New File", 101, true, COLOR_BLACK};
            items_out[count++] = (ExplorerContextItem){"New Folder", 102, true, COLOR_BLACK};
            // Separator logic handled in paint
            items_out[count++] = (ExplorerContextItem){"---", 0, false, 0}; // Marker
            items_out[count++] = (ExplorerContextItem){"Blue", 200, true, COLOR_APPLE_BLUE};
            items_out[count++] = (ExplorerContextItem){"Red", 201, true, COLOR_RED};
            items_out[count++] = (ExplorerContextItem){"Yellow", 202, true, COLOR_APPLE_YELLOW};
            items_out[count++] = (ExplorerContextItem){"Green", 203, true, COLOR_APPLE_GREEN};
            items_out[count++] = (ExplorerContextItem){"Black", 204, true, COLOR_BLACK};
        }
    }
    return count;
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

static void explorer_restore_file(int item_idx) {
    if (item_idx < 0 || item_idx >= item_count) return;
    
    char recycle_path[256];
    explorer_strcpy(recycle_path, current_path);
    if (recycle_path[explorer_strlen(recycle_path) - 1] != '/') explorer_strcat(recycle_path, "/");
    explorer_strcat(recycle_path, items[item_idx].name);
    
    char origin_file_path[256];
    explorer_strcpy(origin_file_path, recycle_path);
    explorer_strcat(origin_file_path, ".origin");
    
    char original_path[256] = {0};
    FAT32_FileHandle *fh = fat32_open(origin_file_path, "r");
    if (fh) {
        int len = fat32_read(fh, original_path, 255);
        if (len > 0) original_path[len] = 0;
        fat32_close(fh);
    }
    
    if (original_path[0] == 0) return; // No origin info
    
    // Restore
    explorer_copy_recursive(recycle_path, original_path);
    explorer_delete_permanently(recycle_path);
    fat32_delete(origin_file_path);
    
    explorer_refresh();
}

static void explorer_load_directory(const char *path) {
    explorer_strcpy(current_path, path);
    
    FAT32_FileInfo *entries = (FAT32_FileInfo*)kmalloc(EXPLORER_MAX_FILES * sizeof(FAT32_FileInfo));
    if (!entries) return;

    int count = fat32_list_directory(path, entries, EXPLORER_MAX_FILES);
    
    item_count = 0;
    for (int i = 0; i < count && i < EXPLORER_MAX_FILES; i++) {
        // Skip .color files
        if (explorer_strcmp(entries[i].name, ".color") == 0) {
            continue;
        }
        
        // Skip .origin files
        if (explorer_str_ends_with(entries[i].name, ".origin")) {
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
    
    kfree(entries);
    selected_item = -1;
    explorer_scroll_row = 0;
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

void explorer_open_directory(const char *path) {
    explorer_load_directory(path);
    win_explorer.visible = true;
    win_explorer.focused = true;
}

static void explorer_open_target(const char *path) {
    if (fat32_is_directory(path)) {
        explorer_open_directory(path);
    } else {
        int max_z = 0;
        if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
        if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
        if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
        if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
        if (win_editor.z_index > max_z) max_z = win_editor.z_index;
        if (win_markdown.z_index > max_z) max_z = win_markdown.z_index;
        if (win_control_panel.z_index > max_z) max_z = win_control_panel.z_index;
        if (win_about.z_index > max_z) max_z = win_about.z_index;
        if (win_minesweeper.z_index > max_z) max_z = win_minesweeper.z_index;

        if (explorer_is_markdown_file(path)) {
            win_markdown.visible = true; win_markdown.focused = true;
            win_markdown.z_index = max_z + 1;
            markdown_open_file(path);
        } else {
            win_editor.visible = true; win_editor.focused = true;
            win_editor.z_index = max_z + 1;
            editor_open_file(path);
        }
    }
}

static void explorer_open_item(int index) {
    if (index < 0 || index >= item_count) return;

    if (items[index].is_directory) {
        explorer_navigate_to(items[index].name);
        return;
    }

    char full_path[256];
    explorer_strcpy(full_path, current_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, items[index].name);

    // Check if shortcut
    if (explorer_str_ends_with(items[index].name, ".shortcut")) {
        Window *target = NULL;
        if (explorer_strcmp(items[index].name, "Notepad.shortcut") == 0) {
            target = &win_notepad; notepad_reset();
        } else if (explorer_strcmp(items[index].name, "Calculator.shortcut") == 0) {
            target = &win_calculator;
        } else if (explorer_strcmp(items[index].name, "Terminal.shortcut") == 0) {
            target = &win_cmd; cmd_reset();
        } else if (explorer_strcmp(items[index].name, "Minesweeper.shortcut") == 0) {
            target = &win_minesweeper;
        } else if (explorer_strcmp(items[index].name, "Control Panel.shortcut") == 0) {
            target = &win_control_panel;
        } else if (explorer_strcmp(items[index].name, "About.shortcut") == 0) {
            target = &win_about;
        } else if (explorer_strcmp(items[index].name, "Explorer.shortcut") == 0) {
            target = &win_explorer; explorer_reset();
        } else if (explorer_strcmp(items[index].name, "Recycle Bin.shortcut") == 0) {
            target = &win_explorer; explorer_load_directory("/RecycleBin");
        }

        if (target) {
            target->visible = true; target->focused = true;
            int max_z = 0;
            if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
            if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
            if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
            if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
            if (win_editor.z_index > max_z) max_z = win_editor.z_index;
            if (win_markdown.z_index > max_z) max_z = win_markdown.z_index;
            if (win_minesweeper.z_index > max_z) max_z = win_minesweeper.z_index;
            if (win_control_panel.z_index > max_z) max_z = win_control_panel.z_index;
            if (win_about.z_index > max_z) max_z = win_about.z_index;
            target->z_index = max_z + 1;
            return;
        }

        // Generic shortcut
        FAT32_FileHandle *fh = fat32_open(full_path, "r");
        if (fh) {
            char buf[256];
            int len = fat32_read(fh, buf, 255);
            fat32_close(fh);
            if (len > 0) {
                buf[len] = 0;
                explorer_open_target(buf);
                return;
            }
        }
    }

    // Default open
    explorer_open_target(full_path);
}

// Draw a simple file icon
static void explorer_draw_file_icon(int x, int y, bool is_dir, uint32_t color, const char *filename) {
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
    } else if (explorer_str_ends_with(filename, ".shortcut")) {
        // App Shortcut - Draw specific icon
        // Strip extension for check
        // Draw icon at x+5, y+5
        // The draw_*_icon functions in wm.c draw at x, y
        // Pass a label, but avoid text drawn by the icon function inside the explorer item
        // because explorer draws its own text. Pass "" as label.
        if (explorer_strcmp(filename, "Notepad.shortcut") == 0) draw_notepad_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Calculator.shortcut") == 0) draw_calculator_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Terminal.shortcut") == 0) draw_terminal_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Minesweeper.shortcut") == 0) draw_minesweeper_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Control Panel.shortcut") == 0) draw_control_panel_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "About.shortcut") == 0) draw_about_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Explorer.shortcut") == 0) draw_folder_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "Recycle Bin.shortcut") == 0) draw_recycle_bin_icon(x + 5, y + 5, "");
        else if (explorer_strcmp(filename, "RecycleBin") == 0) draw_recycle_bin_icon(x + 5, y + 5, "");
        else draw_icon(x + 5, y + 5, "");
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
    
    // Draw scroll buttons (left of dropdown)
    draw_button(win->x + win->w - 160, offset_y + 4, 30, 30, "^", false);
    draw_button(win->x + win->w - 125, offset_y + 4, 30, 30, "v", false);
    
    // Draw file list
    int content_start_y = offset_y + 40;
    
    // Clip content to window area (excluding borders and top bar)
    graphics_set_clipping(win->x + 4, content_start_y, win->w - 8, win->h - 64 - 4);
    
    for (int i = 0; i < item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        // Apply scrolling
        if (row < explorer_scroll_row) continue;
        if (row >= explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        // Draw item background
        uint32_t bg_color = (i == selected_item) ? COLOR_BLUE : COLOR_WHITE;
        draw_bevel_rect(item_x, item_y, EXPLORER_ITEM_WIDTH, EXPLORER_ITEM_HEIGHT, false);
        draw_rect(item_x + 2, item_y + 2, EXPLORER_ITEM_WIDTH - 4, EXPLORER_ITEM_HEIGHT - 4, bg_color);
        
        // Draw icon (larger area)
        explorer_draw_file_icon(item_x + 5, item_y + 5, items[i].is_directory, items[i].color, items[i].name);
        
        // Draw name using intelligent wrapping
        const char *display_name = items[i].name;
        if (explorer_strcmp(items[i].name, "RecycleBin") == 0) {
            display_name = "Recycle Bin";
        }
        explorer_draw_icon_label(item_x, item_y, display_name);
    }
    
    graphics_clear_clipping();
    
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
        if (explorer_str_starts_with(current_path, "/RecycleBin")) {
            draw_string(dlg_x + 10, dlg_y + 35, "This action cannot be undone.", COLOR_BLACK);
            draw_string(dlg_x + 10, dlg_y + 48, "Delete forever?", COLOR_BLACK);
        } else {
            draw_string(dlg_x + 10, dlg_y + 35, "This file will be moved to", COLOR_BLACK);
            draw_string(dlg_x + 10, dlg_y + 45, "the recycle bin.", COLOR_BLACK);
        }
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 65, 80, 25, "Delete", false);
        draw_button(dlg_x + 170, dlg_y + 65, 80, 25, "Cancel", false);
    } else if (dialog_state == DIALOG_REPLACE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_BLACK);
        
        // Message
        draw_string(dlg_x + 10, dlg_y + 35, "Replace existing file?", COLOR_BLACK);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 70, 80, 25, "Replace", false);
        draw_button(dlg_x + 170, dlg_y + 70, 80, 25, "Cancel", false);
    } else if (dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_BLACK);
        
        // Message
        draw_string(dlg_x + 10, dlg_y + 35, "Replace existing file?", COLOR_BLACK);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 70, 80, 25, "Replace", false);
        draw_button(dlg_x + 170, dlg_y + 70, 80, 25, "Cancel", false);
    } else if (dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        // Dialog background
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        // Title
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_BLACK);
        
        // Message
        draw_string(dlg_x + 10, dlg_y + 35, "Overwrite existing file?", COLOR_BLACK);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", COLOR_BLACK);
        
        // Buttons
        draw_button(dlg_x + 50, dlg_y + 70, 80, 25, "Overwrite", false);
        draw_button(dlg_x + 170, dlg_y + 70, 80, 25, "Cancel", false);
    } else if (dialog_state == DIALOG_ERROR) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rect(dlg_x - 5, dlg_y - 5, 310, 120, COLOR_LTGRAY);
        draw_bevel_rect(dlg_x, dlg_y, 300, 110, true);
        
        draw_string(dlg_x + 10, dlg_y + 10, "Error", COLOR_RED);
        draw_string(dlg_x + 10, dlg_y + 40, dialog_input, COLOR_BLACK);
        
        // OK Button
        draw_button(dlg_x + 110, dlg_y + 70, 80, 25, "OK", false);
    }
    
    // Draw context menu if visible
    if (file_context_menu_visible) {
        // Convert window-relative coordinates to screen coordinates for drawing
        int menu_screen_x = win->x + file_context_menu_x;
        int menu_screen_y = win->y + file_context_menu_y;
        
        ExplorerContextItem menu_items[20];
        int count = explorer_build_context_menu(menu_items);
        
        int menu_height = 0;
        for (int i = 0; i < count; i++) {
            if (menu_items[i].action_id == 0) menu_height += 5; // Separator
            else menu_height += CONTEXT_MENU_ITEM_HEIGHT;
        }
        
        // Draw menu background
        draw_rect(menu_screen_x, menu_screen_y, FILE_CONTEXT_MENU_WIDTH, menu_height, COLOR_LTGRAY);
        draw_bevel_rect(menu_screen_x, menu_screen_y, FILE_CONTEXT_MENU_WIDTH, menu_height, true);
        
        int y_offset = 0;
        for (int i = 0; i < count; i++) {
            if (menu_items[i].action_id == 0) {
                draw_rect(menu_screen_x + 2, menu_screen_y + y_offset + 2, FILE_CONTEXT_MENU_WIDTH - 4, 1, COLOR_DKGRAY);
                y_offset += 5;
            } else {
                draw_string(menu_screen_x + 5, menu_screen_y + y_offset + 5, menu_items[i].label, menu_items[i].color);
                y_offset += CONTEXT_MENU_ITEM_HEIGHT;
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
    } else if (dialog_state == DIALOG_REPLACE_CONFIRM) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 50 && x < dlg_x + 130 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_confirm_replace();
            return;
        }
        
        if (x >= dlg_x + 170 && x < dlg_x + 250 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_close();
            return;
        }
    } else if (dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 50 && x < dlg_x + 130 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_confirm_replace_move();
            return;
        }
        
        if (x >= dlg_x + 170 && x < dlg_x + 250 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_close();
            return;
        }
    } else if (dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 50 && x < dlg_x + 130 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_force_create_file();
            return;
        }
        
        if (x >= dlg_x + 170 && x < dlg_x + 250 && y >= dlg_y + 70 && y < dlg_y + 95) {
            dialog_close();
            return;
        }
    } else if (dialog_state == DIALOG_ERROR) {
        int dlg_x = win->w / 2 - 150;
        int dlg_y = win->h / 2 - 60;
        
        if (x >= dlg_x + 110 && x < dlg_x + 190 && y >= dlg_y + 70 && y < dlg_y + 95) {
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
            dialog_open_create_file(current_path);
            return;
        }
        
        // New Folder
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y + dropdown_menu_item_height && 
            y < menu_y + dropdown_menu_item_height * 2) {
            dropdown_menu_toggle();
            dialog_open_create_folder(current_path);
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
    
    // Check scroll buttons
    // Up: w-160
    if (x >= win->w - 160 && x < win->w - 130 &&
        y >= button_y && y < button_y + 30) {
        if (explorer_scroll_row > 0) explorer_scroll_row--;
        return;
    }
    
    // Down: w-125
    if (x >= win->w - 125 && x < win->w - 95 &&
        y >= button_y && y < button_y + 30) {
        int total_rows = (item_count + EXPLORER_COLS - 1) / EXPLORER_COLS;
        if (total_rows == 0) total_rows = 1;
        if (explorer_scroll_row < total_rows - (EXPLORER_ROWS - 1)) explorer_scroll_row++;
        return;
    }
    
    // File items start at y=64 relative to window
    int content_start_y = 64;
    int offset_x = 4;
    
    for (int i = 0; i < item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        // Apply scrolling logic for hit test
        if (row < explorer_scroll_row) continue;
        if (row >= explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        if (x >= item_x && x < item_x + EXPLORER_ITEM_WIDTH &&
            y >= item_y && y < item_y + EXPLORER_ITEM_HEIGHT) {
            
            // Check for double-click
            if (last_clicked_item == i) {
                // Double-click detected
                explorer_open_item(i);
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
    } else if (dialog_state == DIALOG_REPLACE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close();
        } else if (c == '\n') { // Enter
            dialog_confirm_replace();
        }
        return;
    } else if (dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close();
        } else if (c == '\n') { // Enter
            dialog_confirm_replace_move();
        }
        return;
    } else if (dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close();
        } else if (c == '\n') { // Enter
            dialog_force_create_file();
        }
        return;
    } else if (dialog_state == DIALOG_ERROR) {
        if (c == 27 || c == '\n') {
            dialog_close();
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
            // Scroll if needed
            int row = selected_item / EXPLORER_COLS;
            if (row < explorer_scroll_row) explorer_scroll_row = row;
        }
    } else if (c == 18) {  // DOWN
        if (selected_item < item_count - 1) {
            selected_item += EXPLORER_COLS;
            if (selected_item >= item_count) selected_item = item_count - 1;
            // Scroll if needed
            int row = selected_item / EXPLORER_COLS;
            if (row >= explorer_scroll_row + (EXPLORER_ROWS - 1)) explorer_scroll_row = row - (EXPLORER_ROWS - 1) + 1;
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
                explorer_open_item(selected_item);
            }
        }
    } else if (c == 'd' || c == 'D') {  // Delete key
        if (selected_item >= 0) {
            dialog_open_delete_confirm(selected_item);
        }
    } else if (c == 'n' || c == 'N') {  // New file
        dialog_open_create_file(current_path);
    } else if (c == 'f' || c == 'F') {  // New folder
        dialog_open_create_folder(current_path);
    }
}

// === Right-Click Handler ===

static void explorer_handle_right_click(Window *win, int x, int y) {
    (void)win;
    // File items start at y=64 relative to window
    int content_start_y = 64;
    int offset_x = 4;
    
    for (int i = 0; i < item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        // Apply scrolling logic for hit test
        if (row < explorer_scroll_row) continue;
        if (row >= explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
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
    
    // Clicked on empty space
    file_context_menu_visible = true;
    file_context_menu_item = -1; // Background
    file_context_menu_x = x;
    file_context_menu_y = y;
}

static void explorer_handle_file_context_menu_click(Window *win, int x, int y) {
    (void)win;  // Suppress unused warning - absolute coordinates used instead
    
    if (!file_context_menu_visible) {
        return;
    }
    
    // Adjust coordinates to be relative to context menu
    int relative_x = x - file_context_menu_x;
    int relative_y = y - file_context_menu_y;
    
    ExplorerContextItem menu_items[20];
    int count = explorer_build_context_menu(menu_items);
    int menu_height = 0;
    for (int i = 0; i < count; i++) {
        if (menu_items[i].action_id == 0) menu_height += 5; else menu_height += CONTEXT_MENU_ITEM_HEIGHT;
    }
    
    if (relative_x < 0 || relative_x > FILE_CONTEXT_MENU_WIDTH ||
        relative_y < 0 || relative_y > menu_height) {
        // Clicked outside menu - close it
        file_context_menu_visible = false;
        file_context_menu_item = -1;
        return;
    }
    
    // Find clicked item
    int current_y = 0;
    int clicked_action = 0;
    
    for (int i = 0; i < count; i++) {
        int h = (menu_items[i].action_id == 0) ? 5 : CONTEXT_MENU_ITEM_HEIGHT;
        if (relative_y >= current_y && relative_y < current_y + h) {
            if (menu_items[i].enabled && menu_items[i].action_id != 0) {
                clicked_action = menu_items[i].action_id;
            }
            break;
        }
        current_y += h;
    }
    
    if (clicked_action == 0) return;
    
    // Execute Action
    char full_path[256];
    if (file_context_menu_item >= 0) {
        explorer_strcpy(full_path, current_path);
        if (full_path[explorer_strlen(full_path) - 1] != '/') explorer_strcat(full_path, "/");
        explorer_strcat(full_path, items[file_context_menu_item].name);
    }
    
    if (clicked_action == 100) { // Open
        explorer_open_item(file_context_menu_item);
    } else if (clicked_action == 109) { // Open MD
        explorer_open_item(file_context_menu_item);
    } else if (clicked_action == 101) { // New File
        if (file_context_menu_item >= 0 && items[file_context_menu_item].is_directory) {
            dialog_open_create_file(full_path);
        } else {
            dialog_open_create_file(current_path);
        }
    } else if (clicked_action == 102) { // New Folder
        if (file_context_menu_item >= 0 && items[file_context_menu_item].is_directory) {
            dialog_open_create_folder(full_path);
        } else {
            dialog_open_create_folder(current_path);
        }
    } else if (clicked_action == 103) { // Paste
        if (file_context_menu_item >= 0 && items[file_context_menu_item].is_directory) {
            explorer_clipboard_paste(full_path);
        } else {
            explorer_clipboard_paste(current_path);
        }
    } else if (clicked_action == 104) { // Cut
        explorer_clipboard_cut(full_path);
    } else if (clicked_action == 105) { // Copy
        explorer_clipboard_copy(full_path);
    } else if (clicked_action == 106) { // Delete
        dialog_open_delete_confirm(file_context_menu_item);
    } else if (clicked_action == 110) { // Open with Text Editor
        win_editor.visible = true; win_editor.focused = true;
        int max_z = 0;
        if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
        if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
        if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
        if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
        if (win_editor.z_index > max_z) max_z = win_editor.z_index;
        if (win_markdown.z_index > max_z) max_z = win_markdown.z_index;
        if (win_control_panel.z_index > max_z) max_z = win_control_panel.z_index;
        if (win_about.z_index > max_z) max_z = win_about.z_index;
        if (win_minesweeper.z_index > max_z) max_z = win_minesweeper.z_index;
        win_editor.z_index = max_z + 1;
        editor_open_file(full_path);
    } else if (clicked_action == ACTION_RESTORE) {
        explorer_restore_file(file_context_menu_item);
    } else if (clicked_action == ACTION_CREATE_SHORTCUT) {
        explorer_create_shortcut(full_path);
    } else if (clicked_action >= 200 && clicked_action <= 204) { // Colors
        uint32_t new_color = items[file_context_menu_item].color;
        if (clicked_action == 200) new_color = COLOR_APPLE_BLUE;
        else if (clicked_action == 201) new_color = COLOR_RED;
        else if (clicked_action == 202) new_color = COLOR_APPLE_YELLOW;
        else if (clicked_action == 203) new_color = COLOR_APPLE_GREEN;
        else if (clicked_action == 204) new_color = COLOR_BLACK;
        items[file_context_menu_item].color = new_color;
        explorer_set_folder_color(full_path, new_color);
    }
    
    file_context_menu_visible = false;
    file_context_menu_item = -1;
}

// === Drag and Drop Support ===

bool explorer_get_file_at(int screen_x, int screen_y, char *out_path, bool *is_dir) {
    if (!win_explorer.visible) return false;
    
    // Convert screen coordinates to window relative
    int rel_x = screen_x - win_explorer.x;
    int rel_y = screen_y - win_explorer.y;
    
    // Check if inside content area
    if (rel_x < 4 || rel_x > win_explorer.w - 4 || rel_y < 64 || rel_y > win_explorer.h - 4) {
        return false;
    }
    
    int content_start_y = 64;
    int offset_x = 4;
    
    for (int i = 0; i < item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        // Apply scrolling logic for hit test
        if (row < explorer_scroll_row) continue;
        if (row >= explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        if (rel_x >= item_x && rel_x < item_x + EXPLORER_ITEM_WIDTH &&
            rel_y >= item_y && rel_y < item_y + EXPLORER_ITEM_HEIGHT) {
            
            explorer_strcpy(out_path, current_path);
            if (out_path[explorer_strlen(out_path) - 1] != '/') {
                explorer_strcat(out_path, "/");
            }
            explorer_strcat(out_path, items[i].name);
            *is_dir = items[i].is_directory;
            return true;
        }
    }
    return false;
}

void explorer_clear_click_state(void) {
    last_clicked_item = -1;
}

void explorer_refresh(void) {
    explorer_load_directory(current_path);
}

static void explorer_perform_move_internal(const char *source_path, const char *dest_dir) {
    // 1. Extract filename
    char filename[256];
    int len = explorer_strlen(source_path);
    int i = len - 1;
    while (i >= 0 && source_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = source_path[k];
    filename[j] = 0;
    
    // 2. Build dest path
    char dest_path[256];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    // Check if source and dest are the same to prevent deletion
    if (explorer_strcmp(source_path, dest_path) == 0) {
        return;
    }
    
    explorer_copy_recursive(source_path, dest_path);
        
    // 4. Delete source (Move operation)
    explorer_delete_permanently(source_path);
        
    // Refresh
    explorer_refresh();
}

void explorer_import_file_to(const char *source_path, const char *dest_dir) {
    // Check for collision
    char filename[256];
    int len = explorer_strlen(source_path);
    int i = len - 1;
    while (i >= 0 && source_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = source_path[k];
    filename[j] = 0;
    
    char dest_path[256];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') explorer_strcat(dest_path, "/");
    explorer_strcat(dest_path, filename);
    
    if (fat32_exists(dest_path) && explorer_strcmp(source_path, dest_path) != 0) {
        explorer_strcpy(dialog_move_src, source_path);
        explorer_strcpy(dialog_dest_dir, dest_dir);
        dialog_state = DIALOG_REPLACE_MOVE_CONFIRM;
        return;
    }
    
    explorer_perform_move_internal(source_path, dest_dir);
}

void explorer_import_file(const char *source_path) {
    explorer_import_file_to(source_path, current_path);
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
    explorer_scroll_row = 0;
}