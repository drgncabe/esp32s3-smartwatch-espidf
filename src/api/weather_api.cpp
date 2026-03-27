#include <stdlib.h>
#include <string.h>

#include "api/weather_api.h"
#include "context/app_context.h"
#include "core/network/wifi_init.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui/screens/home_page.h"
#include "util/util.h"

static const char *TAG = "weather_api";

double currentTemperature = 0.0;
double currentWeatherCode = 0.0;
WeatherHighLows *globalWeatherData = NULL;
size_t highLowsCount = 0;

bool weatherSuccess = false;

static bool weather_task_running = false;

bool is_weather_valid(const WeatherData &wd)
{
    return wd.hourly_count > 0 &&
           wd.hourly != NULL &&
           wd.current_temperature != 0; // temp=0F is rare in SC; adjust if needed
}

// Manual JSON parsing - avoids cJSON's massive memory overhead
// Parses just what we need without building a full tree

static bool parse_number(const char *json, const char *key, double *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;
    
    pos += strlen(search);
    while (*pos == ' ' || *pos == '\t') pos++;
    
    *out = atof(pos);
    return true;
}

static bool parse_array_start(const char *json, const char *key, const char **array_start) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":[", key);
    const char *pos = strstr(json, search);
    if (!pos) {
        // Try with spaces
        snprintf(search, sizeof(search), "\"%s\" : [", key);
        pos = strstr(json, search);
    }
    if (!pos) return false;
    
    *array_start = strchr(pos, '[');
    if (*array_start) (*array_start)++;
    return true;
}

static int count_array_elements(const char *array_start) {
    int count = 0;
    int depth = 0;
    bool in_string = false;
    
    for (const char *p = array_start; *p && depth >= 0; p++) {
        if (*p == '"' && (p == array_start || *(p-1) != '\\')) {
            in_string = !in_string;
        } else if (!in_string) {
            if (*p == '[') depth++;
            else if (*p == ']') {
                if (depth == 0) break;
                depth--;
            } else if (*p == ',' && depth == 0) {
                count++;
            }
        }
    }
    return count + 1; // Add 1 for the last element
}

WeatherData parse_weather_data(const char *json)
{
    WeatherData weatherData = {};
    weatherData.hourly = NULL;
    weatherData.hourly_count = 0;
    weatherData.current_temperature = 0.0;
    weatherData.current_weather_code = 0.0;

    if (!json || strlen(json) == 0) {
        ESP_LOGE(TAG, "Error: JSON string is NULL or empty");
        return weatherData;
    }

    ESP_LOGI(TAG, "Parsing JSON manually (no cJSON), free heap: %lu", 
             (unsigned long)esp_get_free_heap_size());

    // Parse current temperature and weather code
    const char *current_section = strstr(json, "\"current\"");
    if (current_section) {
        parse_number(current_section, "temperature_2m", &weatherData.current_temperature);
        parse_number(current_section, "weather_code", &weatherData.current_weather_code);
    }

    // Find hourly section
    const char *hourly_section = strstr(json, "\"hourly\"");
    if (!hourly_section) {
        ESP_LOGE(TAG, "No hourly section found");
        return weatherData;
    }

    // Find the arrays
    const char *time_array = NULL;
    const char *temp_array = NULL;
    const char *code_array = NULL;

    if (!parse_array_start(hourly_section, "time", &time_array) ||
        !parse_array_start(hourly_section, "temperature_2m", &temp_array) ||
        !parse_array_start(hourly_section, "weather_code", &code_array)) {
        ESP_LOGE(TAG, "Failed to find hourly arrays");
        return weatherData;
    }

    // Count elements
    size_t count = count_array_elements(time_array);
    ESP_LOGI(TAG, "Found %zu hourly entries, free heap: %lu", 
             count, (unsigned long)esp_get_free_heap_size());

    // Allocate array
    weatherData.hourly = (HourlyWeather *)calloc(count, sizeof(HourlyWeather));
    if (!weatherData.hourly) {
        ESP_LOGE(TAG, "Failed to allocate hourly array");
        return weatherData;
    }
    weatherData.hourly_count = count;

    // Parse each element
    const char *t_pos = time_array;
    const char *temp_pos = temp_array;
    const char *code_pos = code_array;

    for (size_t i = 0; i < count; i++) {
        // Parse time string
        while (*t_pos == ' ' || *t_pos == '\t' || *t_pos == '\n') t_pos++;
        if (*t_pos == '"') {
            t_pos++;
            const char *end = strchr(t_pos, '"');
            if (end) {
                size_t len = end - t_pos;
                if (len > 0 && len < 32) {
                    weatherData.hourly[i].date = (char *)malloc(len + 1);
                    if (weatherData.hourly[i].date) {
                        strncpy(weatherData.hourly[i].date, t_pos, len);
                        weatherData.hourly[i].date[len] = '\0';
                    }
                }
                t_pos = end + 1;
            }
        }
        t_pos = strchr(t_pos, ',');
        if (t_pos) t_pos++;

        // Parse temperature
        while (*temp_pos == ' ' || *temp_pos == '\t' || *temp_pos == '\n') temp_pos++;
        weatherData.hourly[i].temperature = atof(temp_pos);
        temp_pos = strchr(temp_pos, ',');
        if (temp_pos) temp_pos++;

        // Parse weather code
        while (*code_pos == ' ' || *code_pos == '\t' || *code_pos == '\n') code_pos++;
        weatherData.hourly[i].weather_code = atof(code_pos);
        code_pos = strchr(code_pos, ',');
        if (code_pos) code_pos++;
    }

    ESP_LOGI(TAG, "Successfully parsed %zu entries, free heap: %lu", 
             count, (unsigned long)esp_get_free_heap_size());
    return weatherData;
}

WeatherData request_weather(void)
{
    WeatherData wd = {};
    wd.hourly = NULL;
    wd.hourly_count = 0;
    wd.current_temperature = 0.0;
    wd.current_weather_code = 0.0;

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi not connected");
        return wd;
    }

    FriendlyGeocodeData friendlyGeocodeData = g_settings.get_zip_geocode_data(g_settings.get_zip_code());
    
    char url[512];
    snprintf(url, sizeof(url), 
        "http://api.open-meteo.com/v1/forecast?%s&current=temperature_2m,weather_code&hourly=temperature_2m,weather_code&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch&timezone=America%%2FNew_York",
        friendlyGeocodeData.friendlyAPIGeodata);
    
    free(const_cast<char*>(friendlyGeocodeData.friendlyAPIGeodata));
    free(const_cast<char*>(friendlyGeocodeData.friendlyName));

    ESP_LOGI(TAG, "Free heap before request: %lu", (unsigned long)esp_get_free_heap_size());

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 15000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init client");
        return wd;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return wd;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    
    ESP_LOGI(TAG, "Status: %d", status);
    
    if (status != 200) {
        ESP_LOGE(TAG, "Bad status: %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return wd;
    }

    size_t buf_size = 6144;
    // Allocate large buffer in PSRAM to save internal RAM
    char *buffer = (char *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        // Fallback to internal RAM if PSRAM allocation fails
        buffer = (char *)malloc(buf_size);
    }
    if (!buffer) {
        ESP_LOGE(TAG, "No memory for buffer");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return wd;
    }

    ESP_LOGI(TAG, "Reading response...");
    int total = 0;
    int max = buf_size - 1;
    
    while (total < max) {
        int len = esp_http_client_read(client, buffer + total, max - total);
        if (len <= 0) break;
        total += len;
    }
    
    buffer[total] = '\0';
    ESP_LOGI(TAG, "Read %d bytes", total);

    WeatherData result = {};
    if (total > 0) {
        result = parse_weather_data(buffer);
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    ESP_LOGI(TAG, "Free heap after: %lu", (unsigned long)esp_get_free_heap_size());
    
    return result;
}

WeatherData trim_weather_data(const WeatherData *original, size_t increment)
{
    WeatherData trimmedData = {};
    trimmedData.hourly = NULL;
    trimmedData.hourly_count = 0;
    trimmedData.current_temperature = original->current_temperature;
    trimmedData.current_weather_code = original->current_weather_code;

    if (original->hourly_count == 0 || original->hourly == NULL)
    {
        trimmedData.hourly = NULL;
        trimmedData.hourly_count = 0;
        return trimmedData;
    }

    size_t newCount = (original->hourly_count + increment - 1) / increment;
    HourlyWeather *trimmedHourly = (HourlyWeather *)malloc(newCount * sizeof(HourlyWeather));

    ESP_LOGI(TAG, "Trimming: Original count: %zu, New count: %zu", original->hourly_count, newCount);

    for (size_t i = 0, j = 0; i < original->hourly_count && j < newCount; i += increment, j++)
    {
        trimmedHourly[j].temperature = original->hourly[i].temperature;
        trimmedHourly[j].weather_code = original->hourly[i].weather_code;
        if (original->hourly[i].date)
        {
            trimmedHourly[j].date = strdup(original->hourly[i].date);
        }
        else
        {
            trimmedHourly[j].date = NULL;
        }
    }

    trimmedData.hourly = trimmedHourly;
    trimmedData.hourly_count = newCount;
    return trimmedData;
}

char *weatherDataToString(const WeatherData *data)
{
    if (data == NULL || data->hourly == NULL || data->hourly_count == 0)
    {
        return NULL;
    }

    size_t bufferSize = 2048;
    char *buffer = (char *)malloc(bufferSize);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for buffer");
        return NULL;
    }
    buffer[0] = '\0';

    ESP_LOGI(TAG, "Hourly count: %zu", data->hourly_count);

    for (size_t i = 0; i < data->hourly_count; i++)
    {
        char entry[256];
        const char *dateStr = data->hourly[i].date ? data->hourly[i].date : "N/A";
        snprintf(entry, sizeof(entry), "Date: %s, Temp: %.2f, Code: %.2f\n",
                 dateStr, data->hourly[i].temperature, data->hourly[i].weather_code);

        if (strlen(buffer) + strlen(entry) + 1 > bufferSize)
        {
            bufferSize *= 2;
            char *newBuffer = (char *)realloc(buffer, bufferSize);
            if (newBuffer == NULL)
            {
                ESP_LOGE(TAG, "Failed to reallocate memory for buffer");
                free(buffer);
                return NULL;
            }
            buffer = newBuffer;
        }

        strcat(buffer, entry);
    }

    return buffer;
}

WeatherHighLows *parseHighLows(WeatherData weatherData, size_t *outCount)
{
    if (weatherData.hourly_count == 0 || weatherData.hourly == NULL)
    {
        *outCount = 0;
        return NULL;
    }

    WeatherHighLows *highLows = (WeatherHighLows *)malloc(weatherData.hourly_count * sizeof(WeatherHighLows));
    size_t count = 0;

    char currentDay[11] = {0};
    double dailyHigh = -1e9;
    double dailyLow = 1e9;

    // Track most common weather code per day
    int codeHistogram[100] = {0};

    for (size_t i = 0; i < weatherData.hourly_count; i++)
    {
        if (weatherData.hourly[i].date == NULL)
            continue;

        char day[11];
        strncpy(day, weatherData.hourly[i].date, 10);
        day[10] = '\0';

        if (i == 0 || strcmp(currentDay, day) != 0)
        {
            // If not the first entry, finalize the previous day
            if (i != 0)
            {
                // Determine most frequent weather code for the previous day
                int bestCode = 0, bestCount = -1;
                for (int wc = 0; wc < 100; wc++)
                {
                    if (codeHistogram[wc] > bestCount)
                    {
                        bestCount = codeHistogram[wc];
                        bestCode = wc;
                    }
                }

                strncpy(highLows[count].date, currentDay, 10);
                highLows[count].date[10] = '\0';
                highLows[count].high = dailyHigh;
                highLows[count].low = dailyLow;
                highLows[count].weather_code = bestCode;
                count++;
            }

            // Reset for new day
            strncpy(currentDay, day, 10);
            currentDay[10] = '\0';
            dailyHigh = weatherData.hourly[i].temperature;
            dailyLow = weatherData.hourly[i].temperature;
            memset(codeHistogram, 0, sizeof(codeHistogram));
        }
        else
        {
            if (weatherData.hourly[i].temperature > dailyHigh)
                dailyHigh = weatherData.hourly[i].temperature;

            if (weatherData.hourly[i].temperature < dailyLow)
                dailyLow = weatherData.hourly[i].temperature;
        }

        int wc = (int)weatherData.hourly[i].weather_code;
        if (wc >= 0 && wc < 100)
            codeHistogram[wc]++;
    }

    // Add the last day
    if (currentDay[0] != '\0')
    {
        int bestCode = 0, bestCount = -1;
        for (int wc = 0; wc < 100; wc++)
        {
            if (codeHistogram[wc] > bestCount)
            {
                bestCount = codeHistogram[wc];
                bestCode = wc;
            }
        }

        strncpy(highLows[count].date, currentDay, 10);
        highLows[count].date[10] = '\0';
        highLows[count].high = dailyHigh;
        highLows[count].low = dailyLow;
        highLows[count].weather_code = bestCode;
        count++;
    }

    *outCount = count;
    return highLows;
}

const char *translateWeatherCode(double weatherCode)
{
    int code = (int)weatherCode;

    if (code >= 0 && code <= 4)
    {
        return "Clear or Partly Cloudy";
    }
    else if (code >= 5 && code <= 9)
    {
        return "Localized Phenomena";
    }
    else if (code >= 10 && code <= 19)
    {
        return "Mist or Fog";
    }
    else if (code >= 20 && code <= 29)
    {
        return "Drizzle or Rain";
    }
    else if (code >= 30 && code <= 39)
    {
        return "Dust or Sandstorm";
    }
    else if (code >= 40 && code <= 49)
    {
        return "Fog";
    }
    else if (code >= 50 && code <= 59)
    {
        return "Drizzle";
    }
    else if (code >= 60 && code <= 69)
    {
        return "Rain";
    }
    else if (code >= 70 && code <= 79)
    {
        return "Snow";
    }
    else if (code >= 80 && code <= 99)
    {
        return "Showers or Thunderstorms";
    }
    return "Clear";
}

void initializeWeather(void)
{
    ESP_LOGI(TAG, "Initializing weather...");
    
    ESP_LOGI(TAG, "=== HEAP BEFORE REQUEST ===");
    ESP_LOGI(TAG, "Free: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min free ever: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());

    WeatherData weatherData = request_weather();

    if (is_weather_valid(weatherData))
    {
        if (globalWeatherData != NULL)
        {
            free(globalWeatherData);
            globalWeatherData = NULL;
        }

        globalWeatherData = parseHighLows(weatherData, &highLowsCount);
        currentTemperature = weatherData.current_temperature;
        currentWeatherCode = weatherData.current_weather_code;

        ESP_LOGI(TAG, "Weather updated: %.1fF, Code: %.0f, Days: %zu",
                 currentTemperature, currentWeatherCode, highLowsCount);
        weatherSuccess = true;
    }
    else
    {
        ESP_LOGI(TAG, "Weather fetch failed — keeping previous data");
    }

    // Clean up
    if (weatherData.hourly != NULL) {
        for (size_t i = 0; i < weatherData.hourly_count; i++)
        {
            if (weatherData.hourly[i].date != NULL) {
                free(weatherData.hourly[i].date);
            }
        }
        free(weatherData.hourly);
    }
    
    ESP_LOGI(TAG, "=== HEAP AFTER CLEANUP ===");
    ESP_LOGI(TAG, "Free: %lu bytes", (unsigned long)esp_get_free_heap_size());
}

void weather_task(void *parameter)
{
    initializeWeather();

    // Refresh UI safely
    lv_async_call([](void *)
                  { weather_task_running = false; handle_weather_completion_callback(true); }, NULL);

    vTaskDelete(NULL);
}

void handle_weather_completion_callback(bool success){
    refresh_weather_container(success);
}

// async, calls the task above
// core 1 in a background freertos task
void start_weather_task(bool firstConnect)
{
    if(!wifi_is_connected())
    {
        ESP_LOGI(TAG, "WiFi not connected, skipping weather task");
        handle_weather_completion_callback(false);
        return;
    }
    if (firstConnect)
    {
        ESP_LOGI(TAG, "Starting geocode background store task...");
        start_geocode_bg_store_task(firstConnect);
    }
    else
    {
        if(weather_task_running) {
            ESP_LOGI(TAG, "Weather task already running, skipping...");
            return;
        }
        weather_task_running = true;
        xTaskCreatePinnedToCore(
            weather_task,   // function
            "weather_task", // name
            8192,          // stack size
            NULL,          // params
            1,             // priority
            NULL,          // task handle
            1              // core (1 is good; core 0 runs WiFi)
        );
    }
}

void destroy_weather_data(void)
{
    if (globalWeatherData != NULL)
    {
        free(globalWeatherData);
        globalWeatherData = NULL;
    }
    highLowsCount = 0;
    currentTemperature = 0.0;
    currentWeatherCode = 0.0;
}