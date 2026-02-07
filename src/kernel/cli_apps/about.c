#include "cli_utils.h"

void cli_cmd_brewver(char *args) {
    (void)args;
    cli_write("BrewOS v1.40 Alpha\n");
    cli_write("BrewOS Kernel V2.4.0 Pre-Alpha\n");
}
