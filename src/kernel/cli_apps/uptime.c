#include "cli_utils.h"

// Forward declarations from cmd.c
extern void rtc_get_datetime(int *y, int *m, int *d, int *h, int *min, int *s);
extern int boot_time_init;
extern int boot_year, boot_month, boot_day, boot_hour, boot_min, boot_sec;

void cli_cmd_uptime(char *args) {
    (void)args;
    int y, m, d, h, min, s;
    rtc_get_datetime(&y, &m, &d, &h, &min, &s);
    
    int start_sec = boot_hour * 3600 + boot_min * 60 + boot_sec;
    int curr_sec = h * 3600 + min * 60 + s;
    if (curr_sec < start_sec) curr_sec += 24 * 3600;
    
    int diff = curr_sec - start_sec;
    int up_h = diff / 3600;
    int up_m = (diff % 3600) / 60;
    int up_s = diff % 60;
    
    cli_write("Uptime: ");
    cli_write_int(up_h); cli_write("h ");
    cli_write_int(up_m); cli_write("m ");
    cli_write_int(up_s); cli_write("s\n");
}
