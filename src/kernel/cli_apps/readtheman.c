#include "cli_utils.h"

// Forward declaration from cmd.c
extern void cli_cmd_beep(char *args);

void cli_cmd_readtheman(char *args) {
    (void)args;
    cli_write("\nYou read the manual? NERD. you know what?\n");
    cli_write("Fuck you.\n");
    for(int i=0; i<3; i++) {
        cli_cmd_beep(NULL);
        cli_delay(1000000);
    }
}
