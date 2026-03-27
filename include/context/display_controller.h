#pragma once

#include <stdint.h>

class DisplayController {
public:
    DisplayController(int blPin, int blChannel);

    void begin();
    void set_brightness(uint8_t lvl);
    uint8_t get_brightness() const;

private:
    int m_blPin;
    int m_blChannel;
    uint8_t m_brightness = 200;
};
