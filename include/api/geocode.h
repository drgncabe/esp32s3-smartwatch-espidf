#pragma once

#ifndef GEOCODE_H
#define GEOCODE_H

typedef struct {
    double latitude;
    double longitude;
    const char *placeName;
    const char *stateAbr;
} GeocodeData;

typedef struct {
    const char *friendlyName;
    const char *friendlyAPIGeodata;
} FriendlyGeocodeData;

GeocodeData get_lat_lon_from_zip(int zip);
void start_geocode_bg_store_task(bool firstConnect = false);

#endif
