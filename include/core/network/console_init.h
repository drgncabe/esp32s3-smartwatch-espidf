#pragma once

#ifndef CONSOLE_INIT_H
#define CONSOLE_INIT_H

void initialize_console();
void handle_wifi_console();
void stop_console();
void start_console();

extern char consoleIp[16];

#endif
