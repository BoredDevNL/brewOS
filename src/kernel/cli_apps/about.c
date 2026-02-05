#include "cli_utils.h"

void cli_cmd_brewver(char *args) {
    (void)args;
    cli_write("BrewOS v1.02 Alpha\n");
    cli_write("BrewOS Kernel V2.0.2 Pre-Alpha\n");
}
