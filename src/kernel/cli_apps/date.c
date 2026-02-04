#include "cli_utils.h"

// Forward declaration from cmd.c
extern void rtc_get_datetime(int *y, int *m, int *d, int *h, int *min, int *s);

void cli_cmd_date(char *args) {
    (void)args;
    int y, m, d, h, min, s;
    rtc_get_datetime(&y, &m, &d, &h, &min, &s);
    cli_write("Current Date: ");
    cli_write_int(y); cli_write("-"); cli_write_int(m); cli_write("-"); cli_write_int(d);
    cli_write(" ");
    cli_write_int(h); cli_write(":"); cli_write_int(min); cli_write(":"); cli_write_int(s);
    cli_write("\n");
}
