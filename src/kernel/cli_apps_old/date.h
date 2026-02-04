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
#ifndef DATE_H
#define DATE_H

#include "print.h"
#include "keyboard.h"
#include "rtc.h"
#include "timezones.h"

// String comparison function for kernel
static int strcmp_kernel_date(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// Simple delay function
static inline void brewing_date(int iterations) {
    for (volatile int i = 0; i < iterations; i++) {
        __asm__ __volatile__("nop");
    }
}

// --- Timezone Selection ---
static void select_timezone_for_continent_date(const char* continent, int* timezone_offset_h, int* timezone_offset_m) {
    int selected_timezone = 0;
    int needs_redraw = 1;

    while (1) {
        if (needs_redraw) {
            print_clear();
            brew_str("Select a timezone:\n");
            int current_timezone = 0;
            for (int i = 0; i < num_timezones; i++) {
                if (strcmp_kernel_date(timezones[i].continent, continent) == 0) {
                    if (current_timezone == selected_timezone) {
                        print_set_color(PRINT_INDEX_0, PRINT_INDEX_7);
                    }
                    brew_str(timezones[i].name);
                    brew_str("\n");
                    print_set_color(PRINT_INDEX_7, PRINT_INDEX_0);
                    current_timezone++;
                }
            }
            needs_redraw = 0;
        }

        while (!check_keyboard()) { /* Do nothing */ }

        unsigned char sc = read_scan_code();
        if (sc == 0x48) { // Up Arrow
            if (selected_timezone > 0) {
                selected_timezone--;
                needs_redraw = 1;
            }
        } else if (sc == 0x50) { // Down Arrow
            int max_timezone = 0;
            for (int i = 0; i < num_timezones; i++) {
                if (strcmp_kernel_date(timezones[i].continent, continent) == 0) {
                    max_timezone++;
                }
            }
            if (selected_timezone < max_timezone - 1) {
                selected_timezone++;
                needs_redraw = 1;
            }
        } else if (scan_code_to_ascii(sc) == '\n' || scan_code_to_ascii(sc) == '\r') {
            int current_timezone = 0;
            for (int i = 0; i < num_timezones; i++) {
                if (strcmp_kernel_date(timezones[i].continent, continent) == 0) {
                    if (current_timezone == selected_timezone) {
                        *timezone_offset_h = timezones[i].offset_h; // DST
                        *timezone_offset_m = timezones[i].offset_m;
                        return;
                    }
                    current_timezone++;
                }
            }
        }
        brewing_date(10000000);
    }
}

static void select_continent_date(int* timezone_offset_h, int* timezone_offset_m) {
    const char* continents[] = {"North America", "South America", "Europe", "Asia", "Oceania", "Africa"};
    int num_continents = sizeof(continents) / sizeof(char*);
    int selected_continent = 0;
    int needs_redraw = 1;

    while (1) {
        if (needs_redraw) {
            print_clear();
            brew_str("Select a continent:\n");
            for (int i = 0; i < num_continents; i++) {
                if (i == selected_continent) {
                    print_set_color(PRINT_INDEX_0, PRINT_INDEX_7);
                }
                brew_str(continents[i]);
                brew_str("\n");
                print_set_color(PRINT_INDEX_7, PRINT_INDEX_0);
            }
            needs_redraw = 0;
        }

        while (!check_keyboard()) { /* Do nothing */ }

        unsigned char sc = read_scan_code();
        if (sc == 0x48) { // Up Arrow
            if (selected_continent > 0) {
                selected_continent--;
                needs_redraw = 1;
            }
        } else if (sc == 0x50) { // Down Arrow
            if (selected_continent < num_continents - 1) {
                selected_continent++;
                needs_redraw = 1;
            }
        } else if (scan_code_to_ascii(sc) == '\n' || scan_code_to_ascii(sc) == '\r') {
            select_timezone_for_continent_date(continents[selected_continent], timezone_offset_h, timezone_offset_m);
            return;
        }
        brewing_date(10000000);
    }
}

// Function to print a zero-padded integer
static void brew_int_padded_date(int n) {
    if (n >= 0 && n < 10) {
        print_char('0');
    }
    brew_int(n);
}

static void date_command(int* timezone_offset_h, int* timezone_offset_m) {
    select_continent_date(timezone_offset_h, timezone_offset_m);
    int year, month, day, hour, minute, second;
    get_datetime(&year, &month, &day, &hour, &minute, &second);

    // Apply timezone offset
    hour += *timezone_offset_h;
    minute += *timezone_offset_m;

    // Handle minute overflow/underflow
    if (minute >= 60) {
        hour++;
        minute -= 60;
    } else if (minute < 0) {
        hour--;
        minute += 60;
    }

    // Handle hour overflow/underflow
    if (hour >= 24) {
        day++;
        hour -= 24;
    } else if (hour < 0) {
        day--;
        hour += 24;
    }

    // Note: This doesn't handle day/month/year wrapping correctly. It's a simplified implementation.

    brew_str("\nCurrent Date and Time:\n");
    brew_int(year);
    brew_str("-");
    brew_int_padded_date(month);
    brew_str("-");
    brew_int_padded_date(day);
    brew_str(" ");
    brew_int_padded_date(hour);
    brew_str(":");
    brew_int_padded_date(minute);
    brew_str(":");
    brew_int_padded_date(second);
    brew_str("\n");
}

#endif // DATE_H
