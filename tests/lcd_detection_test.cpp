#include <stdio.h>
#include <stdlib.h>
#include "../include/lcd_detection.h"

using namespace natal;

void check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main() {
    for (uint8_t expected : LCD_ADDRESSES) {
        LcdScanResult result = detectLcd([expected](uint8_t address) {
            return static_cast<uint8_t>(address == expected ? 0 : 2);
        });
        check(result.status == LcdScanStatus::Found && result.address == expected,
              "each supported address is selected");
    }
    LcdScanResult absent = detectLcd([](uint8_t) { return uint8_t(2); });
    check(absent.status == LcdScanStatus::NotFound, "no device");
    LcdScanResult multiple = detectLcd([](uint8_t address) {
        return static_cast<uint8_t>(address == 0x27 ? 2 : 0);
    });
    check(multiple.status == LcdScanStatus::Multiple, "multiple devices");
    LcdScanResult error = detectLcd([](uint8_t address) {
        return static_cast<uint8_t>(address == 0x23 ? 0 : address == 0x27 ? 5 : 2);
    });
    check(error.status == LcdScanStatus::BusError && error.address == 0x27 && error.error == 5,
          "bus error prevents selection even after an acknowledgement");
    puts("PASS: LCD address detection");
    return 0;
}
