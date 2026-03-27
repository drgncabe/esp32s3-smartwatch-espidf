#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <stddef.h>

typedef struct {
    double temperature;
    double weather_code;
    char *date;
} HourlyWeather;

typedef struct {
    double current_temperature;
    double current_weather_code;
    HourlyWeather *hourly;
    size_t hourly_count;
} WeatherData;

typedef struct {
    char date[11];
    double high;
    double low;
    double weather_code;
} WeatherHighLows;

extern double currentTemperature;
extern double currentWeatherCode;
extern WeatherHighLows *globalWeatherData;
extern size_t highLowsCount;
extern bool weatherSuccess;

void initialize_weather(void);
void destroy_weather_data(void);
void start_weather_task(bool firstConnect = false);
void handle_weather_completion_callback(bool success = true);

#endif
