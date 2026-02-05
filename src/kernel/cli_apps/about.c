#include "cli_utils.h"

void cli_cmd_brewver(char *args) {
    (void)args;
    cli_write("BrewOS v1.10 Alpha\n");
    cli_write("BrewOS Kernel V2.1.0 Pre-Alpha\n");
}
