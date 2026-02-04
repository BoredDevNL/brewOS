#include "cli_utils.h"
#include "fat32.h"
#include "wm.h"

// Forward declarations from editor.h and wm.c
extern void editor_open_file(const char *filename);
extern void editor_init(void);
extern Window win_editor;
extern Window win_explorer;
extern Window win_cmd;
extern Window win_notepad;
extern Window win_calculator;

void cli_cmd_txtedit(char *args) {
    // Parse the file path argument
    char filepath[256];
    int i = 0;
    
    // Skip leading whitespace
    while (args && args[i] && (args[i] == ' ' || args[i] == '\t')) {
        i++;
    }
    
    // Extract filepath
    int j = 0;
    while (args && args[i] && args[i] != ' ' && args[i] != '\t' && j < 255) {
        filepath[j++] = args[i++];
    }
    filepath[j] = 0;
    
    // If no filepath provided, create a new empty file
    if (j == 0) {
        cli_write("Usage: txtedit <filename>\n");
        cli_write("Example: txtedit myfile.txt\n");
        cli_write("         txtedit /document.txt\n");
        return;
    }
    
    // Normalize the path (handles relative and absolute paths)
    char normalized_path[256];
    fat32_normalize_path(filepath, normalized_path);
    
    // Open the file in the GUI editor
    editor_open_file(normalized_path);
    
    // Make editor window visible and focused, bring to front
    win_editor.visible = true;
    win_editor.focused = true;
    
    // Calculate max z_index to bring window to front
    int max_z = 0;
    if (win_explorer.z_index > max_z) max_z = win_explorer.z_index;
    if (win_cmd.z_index > max_z) max_z = win_cmd.z_index;
    if (win_notepad.z_index > max_z) max_z = win_notepad.z_index;
    if (win_calculator.z_index > max_z) max_z = win_calculator.z_index;
    win_editor.z_index = max_z + 1;
    
    cli_write("Opening: ");
    cli_write(normalized_path);
    cli_write("\n");
}
