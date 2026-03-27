#ifndef TIME_FORMATTING_H
#define TIME_FORMATTING_H

#include <cstddef>
#include <ctime>

void format_time_12hour(char *buf, size_t buf_size, bool include_prefix = false);
void format_time_12hour_from_time(char *buf, size_t buf_size, time_t now, bool include_prefix = false);
void format_current_date(char *buffer, size_t size);
const char *get_day_of_week_from_date(const char *date_str);

#endif
