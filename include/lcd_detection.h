#pragma once

#include <stdint.h>

namespace natal {

constexpr uint8_t LCD_COLUMNS = 20;
constexpr uint8_t LCD_ROWS = 4;
constexpr uint8_t LCD_ADDRESSES[] = {0x23, 0x27, 0x3f};

enum class LcdScanStatus : uint8_t { Found, NotFound, Multiple, BusError };

struct LcdScanResult {
    LcdScanStatus status;
    uint8_t address;
    uint8_t error;
};

template <typename Probe>
LcdScanResult detectLcd(Probe probe) {
    uint8_t found = 0;
    bool multiple = false;
    for (uint8_t address : LCD_ADDRESSES) {
        const uint8_t result = probe(address);
        if (result == 0) {
            if (found) multiple = true;
            else found = address;
        } else if (result != 2) {
            return {LcdScanStatus::BusError, address, result};
        }
    }
    if (multiple) return {LcdScanStatus::Multiple, 0, 0};
    if (!found) return {LcdScanStatus::NotFound, 0, 0};
    return {LcdScanStatus::Found, found, 0};
}

}
