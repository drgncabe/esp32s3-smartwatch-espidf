#pragma once

#ifndef PREFS_H
#define PREFS_H

#include <stdint.h>
#include "api/geocode.h"

#define APP_NAMESPACE "watch_os"
#define CREDENTIALS_NAMESPACE "credentials"
#define GEOCODE_NAMESPACE "geocode"
#define HEARTBEAT_NAMESPACE "hb"

typedef struct {
    const char *ssid;
    const char *password;
} network_pref_data_t;

extern const char *heartbeatNamespace;

// Read functions
bool write_pref_char(const char *key, const char *charValue, const char *nspace = APP_NAMESPACE);
bool write_pref_uint(const char *key, const uint8_t *uint8Value, const char *nspace = APP_NAMESPACE);
bool write_pref_int(const char *key, const int *intValue, const char *nspace = APP_NAMESPACE);
bool write_pref_bool(const char *key, const bool *boolValue, const char *nspace = APP_NAMESPACE);
bool write_pref_long(const char *key, const long *longValue, const char *nspace = APP_NAMESPACE);

// Write functions
char *read_pref_string(const char *key, const char *defaultValue, const char *nspace = APP_NAMESPACE);
uint8_t read_pref_uint(const char *key, const uint8_t defaultValue, const char *nspace = APP_NAMESPACE);
int read_pref_int(const char *key, const int defaultValue, const char *nspace = APP_NAMESPACE);
long read_pref_long(const char *key, const long defaultValue, const char *nspace = APP_NAMESPACE);
bool read_pref_bool(const char *key, const bool defaultValue, const char *nspace = APP_NAMESPACE);

// Data specific functions
network_pref_data_t get_saved_network_prefs(const char *ssid);
void save_network_prefs(const char *ssid, const char *password);

void save_geocode_data(int zipCode, double latitude, double longitude, const char *placeName, const char *stateAbr);
FriendlyGeocodeData get_saved_geocode_data(int zipCode, const char *defaultValue);
void clear_geocode_namespace();

#endif
