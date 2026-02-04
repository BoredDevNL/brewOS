#ifndef CLI_COMMAND_H
#define CLI_COMMAND_H

#include <stddef.h>

// Standard interface for CLI command output
// Commands should call these functions to write to the terminal
extern void cli_write(const char *str);
extern void cli_write_int(int n);
extern void cli_putchar(char c);

// Callback function type for command execution
typedef void (*cmd_callback_t)(char *args);

// Command entry in dispatch table
typedef struct {
    const char *name;
    cmd_callback_t callback;
    const char *help_text;
} CLI_Command;

#endif
