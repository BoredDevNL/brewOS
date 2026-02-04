#ifndef RTC_H
#define RTC_H

#include <stdint.h>

void rtc_get_datetime(int *year, int *month, int *day, int *hour, int *minute, int *second);

#endif
