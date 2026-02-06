#ifndef CMD_H
#define CMD_H

#include "wm.h"

extern Window win_cmd;

void cmd_init(void);
void cmd_reset(void);

// IO Functions
void cmd_write(const char *str);
void cmd_putchar(char c);
void cmd_write_int(int n);
void cmd_screen_clear(void);

#endif