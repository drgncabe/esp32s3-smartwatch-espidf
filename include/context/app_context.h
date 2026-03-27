#pragma once

#include "context/app_settings.h"
#include "context/display_controller.h"

extern AppSettings g_settings;
extern DisplayController g_display;

void play_haptic_soft();
void play_haptic_medium();
void play_haptic_hard();
void play_haptic_error();
void play_haptic_notification(bool force = false);
void play_haptic_click();
