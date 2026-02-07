#include "cli_utils.h"

void cli_cmd_brewver(char *args) {
    (void)args;
    cli_write("BrewOS v1.40 Beta\n");
    cli_write("BrewOS Kernel V2.3.1 Beta\n");
}
