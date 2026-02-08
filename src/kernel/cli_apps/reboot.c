#include "cli_utils.h"
#include "io.h"

void cli_cmd_reboot(char *args) {
    (void)args;
    cli_write("Rebooting...\n");
    cli_sleep(100);
    while ((inb(0x64) & 2) != 0) cli_sleep(1);
    outb(0x64, 0xFE);
    asm volatile ("int $0x3");
}
