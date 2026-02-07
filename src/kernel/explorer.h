#ifndef EXPLORER_H
#define EXPLORER_H

#include "wm.h"

extern Window win_explorer;
extern Window win_editor;
extern Window win_cmd;
extern Window win_notepad;
extern Window win_calculator;
extern Window win_markdown;

void explorer_init(void);
void explorer_reset(void);
void explorer_refresh(void);
void explorer_clear_click_state(void);

// Drag and Drop support
bool explorer_get_file_at(int screen_x, int screen_y, char *out_path, bool *is_dir);
void explorer_import_file(const char *source_path);
void explorer_import_file_to(const char *source_path, const char *dest_dir);

// Clipboard
void explorer_clipboard_copy(const char *path);
void explorer_clipboard_cut(const char *path);
void explorer_clipboard_paste(const char *dest_dir);
bool explorer_clipboard_has_content(void);

// File Operations
bool explorer_delete_permanently(const char *path);
bool explorer_delete_recursive(const char *path);
void explorer_create_shortcut(const char *target_path);

void explorer_open_directory(const char *path);

#endif
