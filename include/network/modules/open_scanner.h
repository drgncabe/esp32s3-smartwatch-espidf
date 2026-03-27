#pragma once

#ifndef OPEN_SCANNER_H
#define OPEN_SCANNER_H

extern const char *open_scanner_status_text;

void start_open_scanner();
void try_open_network_sync(bool isTimeSync = true);
bool is_open_scanner_running();
void start_open_time_task();
void check_periodic_open_scanner();

#endif
