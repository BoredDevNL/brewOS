#include "cli_utils.h"

// Forward declarations - these will be provided by cmd.c
extern void cmd_putchar(char c);
extern void cmd_write(const char *str);
extern void cmd_write_int(int n);

void cli_memset(void *dest, int val, size_t len) {
    unsigned char *ptr = dest;
    while (len-- > 0) *ptr++ = val;
}

size_t cli_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int cli_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void cli_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

int cli_atoi(const char *str) {
    int res = 0;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res * sign;
}

void cli_itoa(int n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    int i = 0;
    int sign = n < 0;
    if (sign) n = -n;
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    if (sign) buf[i++] = '-';
    buf[i] = 0;
    // Reverse
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

void cli_write(const char *str) {
    cmd_write(str);
}

void cli_write_int(int n) {
    cmd_write_int(n);
}

void cli_putchar(char c) {
    cmd_putchar(c);
}

void cli_delay(int iterations) {
    for (volatile int i = 0; i < iterations; i++) {
        __asm__ __volatile__("nop");
    }
}
