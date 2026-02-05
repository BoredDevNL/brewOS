#include "cli_utils.h"

void cli_cmd_help(char *args) {
    (void)args;
    cli_write("Available commands:\n");
    cli_write("  HELP     - Display this help message\n");
    cli_write("  DATE     - Display current date and time\n");
    cli_write("  CLEAR    - Clear the screen\n");
    cli_write("  BREWVER  - Gives version info\n");
    cli_write("  MATH     - math <op> <a> <b> (e.g. math + 1 2)\n");
    cli_write("  MAN      - Show user manual (interactive)\n");
    cli_write("  LICENSE  - Show license (interactive)\n");
    cli_write("  UPTIME   - System uptime\n");
    cli_write("  BEEP     - Make a sound\n");
    cli_write("  COWSAY   - cowsay <msg>\n");
    cli_write("  REBOOT   - Reboot system\n");
    cli_write("  SHUTDOWN - Shutdown system\n");
    cli_write("  MEMINFO  - Gives memory info\n");
}
