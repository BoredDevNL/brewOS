#include "cli_utils.h"

void cli_cmd_brewver(char *args) {
    (void)args;
    cli_write("BrewOS v1.03 Alpha\n");
    cli_write("BrewOS Kernel V2.0.3 Pre-Alpha\n");
}
