#pragma once

#ifndef BOOT_H
#define BOOT_H

void power_button_task(void *arg);
void latch_power(void);
void init_power_button_handler(void);

#endif
