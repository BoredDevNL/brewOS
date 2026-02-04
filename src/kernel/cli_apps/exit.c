#include "cli_utils.h"

// Public declaration from cmd.c
extern void cmd_window_exit(void);

void cli_cmd_exit(char *args) {
    (void)args;
    cmd_window_exit();
}
