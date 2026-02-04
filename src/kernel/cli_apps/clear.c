#include "cli_utils.h"

// Public declaration from cmd.c
extern void cmd_screen_clear(void);

void cli_cmd_clear(char *args) {
    (void)args;
    cmd_screen_clear();
}
