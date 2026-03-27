#include "ui/components/time_formatting.h"
#include <cstdio>
#include <cstring>

void format_time_12hour(char *buf, size_t buf_size, bool include_prefix)
{
    time_t now;
    time(&now);
    format_time_12hour_from_time(buf, buf_size, now, include_prefix);
}

void format_time_12hour_from_time(char *buf, size_t buf_size, time_t now, bool include_prefix)
{
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    const char *ampm = (hour >= 12) ? "PM" : "AM";
    
    // Convert to 12-hour format
    if (hour == 0) 
        hour = 12;
    else if (hour > 12) 
        hour -= 12;
    
    if (include_prefix)
        snprintf(buf, buf_size, "Time: %02d:%02d %s", hour, minute, ampm);
    else
        snprintf(buf, buf_size, "%02d:%02d %s", hour, minute, ampm);
}

void format_current_date(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d", t);
}

const char *get_day_of_week_from_date(const char *date_str)
{
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    int year, month, day;
    if (sscanf(date_str, "%d-%d-%d", &year, &month, &day) != 3)
    {
        return "???";
    }

    struct tm timeinfo = {};
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    mktime(&timeinfo);

    return days[timeinfo.tm_wday];
}
