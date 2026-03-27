#include "context/app_context.h"

AppSettings g_settings;

void play_haptic_soft() {
    if (g_settings.get_haptics_enabled() == 1) {
        // TODO: Implement haptic soft
    }
}

void play_haptic_medium() {
    if (g_settings.get_haptics_enabled() == 1) {
        // TODO: Implement haptic medium
    }
}

void play_haptic_hard() {
    if (g_settings.get_haptics_enabled() == 1) {
        // TODO: Implement haptic hard
    }
}

void play_haptic_error() {
    if (g_settings.get_haptics_enabled() == 1) {
        // TODO: Implement haptic error
    }
}

void play_haptic_notification(bool force) {
    if (g_settings.get_haptics_enabled() == 1 || force) {
        // TODO: Implement haptic notification
    }
}

void play_haptic_click() {
    if (g_settings.get_haptics_enabled() == 1) {
        // TODO: Implement haptic click
    }
}