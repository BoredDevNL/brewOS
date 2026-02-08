#include "cli_utils.h"
#include "io.h"

void cli_cmd_shutdown(char *args) {
    (void)args;
    cli_write("Shutting down...\n");
    cli_sleep(100);
    outb(0x64, 0xFE);
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
}
