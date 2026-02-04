#include "cli_utils.h"

// Forward declaration from cmd.c
extern void pager_wrap_content(const char **lines, int count);
extern void pager_set_mode(void);

const char* manual_pages[] = {
    "BrewKernel User Manual",
    "----------------------",
    "",
    "Welcome to the BrewKernel.",
    "",
    "== Features ==",
    "* Ramdisk-based Filesystem: A simple in-memory filesystem.",
    "* VGA Text Mode Driver: Full control over text/colors.",
    "* PS/2 Keyboard Driver: Handles key presses.",
    "* Simple CLI: A basic shell.",
    "",
    "== Available Commands ==",
    "HELP: Displays a short list of available commands.",    
    "MAN: Shows this detailed user manual.",
    "ABOUT: Displays information about the kernel.",
    "MATH: A simple calculator.",
    "DATE: Displays the current date and time.",
    "TXTEDIT: A simple text editor with file path support.",
    "  USAGE: txtedit <filename>",
    "  EXAMPLES:",
    "    txtedit file.txt       (relative path in current directory)",
    "    txtedit /file.txt      (absolute path in root)",
    "    txtedit /docs/note.txt (absolute path with subdirectories)",
    "  FEATURES: Create/Edit files, Save (to RAM), Navigation.",
    "CLEAR: Clears the entire screen.",
    "EXIT: Exits the CLI mode.",
    "LICENSE: Displays the full GNU GPL v3.",
    "COWSAY: Moo!",    
    "UPTIME: Shows how long the system has been running.",
    "BEEP: Makes a beep sound.",
    "--- End of Manual ---"
};
const int manual_num_lines = sizeof(manual_pages) / sizeof(char*);

void cli_cmd_man(char *args) {
    (void)args;
    pager_wrap_content(manual_pages, manual_num_lines);
    pager_set_mode();
}
