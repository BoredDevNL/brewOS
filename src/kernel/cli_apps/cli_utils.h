#ifndef CLI_UTILS_H
#define CLI_UTILS_H

#include <stddef.h>
#include <stdint.h>

// String utilities
void cli_memset(void *dest, int val, size_t len);
size_t cli_strlen(const char *str);
int cli_strcmp(const char *s1, const char *s2);
void cli_strcpy(char *dest, const char *src);
int cli_atoi(const char *str);
void cli_itoa(int n, char *buf);

// IO utilities
void cli_write(const char *str);
void cli_write_int(int n);
void cli_putchar(char c);

// Timing utility
void cli_delay(int iterations);

// CLI Command declarations
void cli_cmd_shutdown(char *args);
void cli_cmd_reboot(char *args);

#endif
