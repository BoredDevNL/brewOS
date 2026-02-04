#include "cli_utils.h"
#include "io.h"

void cli_cmd_beep(char *args) {
    (void)args;
    cli_write("BEEP!\n");
    outb(0x43, 0xB6);
    int freq = 1000;
    int div = 1193180 / freq;
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    
    outb(0x61, inb(0x61) | 0x03);
    cli_delay(10000000);
    outb(0x61, inb(0x61) & 0xFC);
}
