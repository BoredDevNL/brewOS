/*
 * Brew Kernel
 * Copyright (C) 2024-2026 boreddevnl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef UPTIME_H
#define UPTIME_H

#include "print.h"
#include "rtc.h"

// Global variable to store boot time
static int boot_time_hour = -1;
static int boot_time_min = -1;
static int boot_time_sec = -1;

// Initialize boot time
void init_uptime(void) {
    if (boot_time_hour == -1) {
        int year, month, day;
        get_datetime(&year, &month, &day, &boot_time_hour, &boot_time_min, &boot_time_sec);
    }
}

// Function to print a zero-padded integer
static void brew_int_padded_uptime(int n) {
    if (n >= 0 && n < 10) {
        print_char('0');
    }
    brew_int(n);
}

// Calculate time difference considering wraparound
static void calc_time_diff(int start_h, int start_m, int start_s,
                         int curr_h, int curr_m, int curr_s,
                         int *diff_h, int *diff_m, int *diff_s) {
    int total_start_secs = start_h * 3600 + start_m * 60 + start_s;
    int total_curr_secs = curr_h * 3600 + curr_m * 60 + curr_s;
    
    // Handle day wraparound
    if (total_curr_secs < total_start_secs) {
        total_curr_secs += 24 * 3600;  // Add a full day of seconds
    }
    
    int diff_secs = total_curr_secs - total_start_secs;
    
    *diff_h = diff_secs / 3600;
    diff_secs %= 3600;
    *diff_m = diff_secs / 60;
    *diff_s = diff_secs % 60;
}

// Display uptime command
static void display_uptime(void) {
    init_uptime();  // Ensure boot time is initialized
    
    int curr_hour, curr_min, curr_sec;
    int year, month, day;
    get_datetime(&year, &month, &day, &curr_hour, &curr_min, &curr_sec);
    
    int up_hours, up_minutes, up_seconds;
    calc_time_diff(boot_time_hour, boot_time_min, boot_time_sec,
                  curr_hour, curr_min, curr_sec,
                  &up_hours, &up_minutes, &up_seconds);
    
    brew_str("\nSystem uptime: ");
    if (up_hours > 0) {
        brew_int(up_hours);
        brew_str(" hour");
        if (up_hours != 1) brew_str("s");
        brew_str(" ");
    }
    if (up_minutes > 0 || up_hours > 0) {
        brew_int(up_minutes);
        brew_str(" minute");
        if (up_minutes != 1) brew_str("s");
        brew_str(" ");
    }
    brew_int(up_seconds);
    brew_str(" second");
    if (up_seconds != 1) brew_str("s");
    brew_str("\n");
}

#endif // UPTIME_H