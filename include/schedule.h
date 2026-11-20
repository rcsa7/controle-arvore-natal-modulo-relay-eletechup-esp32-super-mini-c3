#pragma once

#include <stdint.h>
#include <stddef.h>

namespace natal {

constexpr uint8_t RELAY_COUNT = 4;
constexpr uint8_t MAX_RULES = 16;
// Ordem CH1 a CH4: ligacoes internas da ESC3E05 aos GPIOs da ESP32-C3 Super Mini.
constexpr uint8_t RELAY_PINS[RELAY_COUNT] = {
    5, 6, 7, 10
};
constexpr uint8_t RELAY_ON_LEVEL = 1; // HIGH por padrao; confirme sem cargas conectadas.

struct Rule {
    bool enabled = true;
    bool monthly = false;
    uint8_t days = 127;  // bit 0 = domingo
    uint8_t monthDay = 1;
    uint16_t on = 1080;
    uint16_t off = 1380;
};

struct RelayConfig {
    char name[49] = "";
    uint8_t count = 0;
    Rule rules[MAX_RULES];
};

struct LocalMinute {
    uint8_t weekday;
    uint8_t monthDay;
    uint8_t previousMonthDay;
    uint16_t minute;
};

inline bool parseMinute(const char* value, uint16_t& result) {
    if (!value) return false;
    size_t length = 0;
    while (value[length] && length < 6) ++length;
    if (length != 5 || value[2] != ':') return false;
    for (uint8_t i = 0; i < 5; ++i) {
        if (i == 2) continue;
        if (value[i] < '0' || value[i] > '9') return false;
    }
    const int hour = (value[0] - '0') * 10 + value[1] - '0';
    const int minute = (value[3] - '0') * 10 + value[4] - '0';
    if (hour > 23 || minute > 59) return false;
    result = hour * 60 + minute;
    return true;
}

inline bool selected(const Rule& rule, uint8_t weekday, uint8_t monthDay) {
    return rule.monthly ? rule.monthDay == monthDay : (rule.days & (1U << weekday)) != 0;
}

inline bool active(const Rule& rule, const LocalMinute& now) {
    if (!rule.enabled || rule.on == rule.off) return false;
    const bool today = selected(rule, now.weekday, now.monthDay);
    if (rule.on < rule.off) return today && now.minute >= rule.on && now.minute < rule.off;
    const bool yesterday = selected(rule, (now.weekday + 6) % 7, now.previousMonthDay);
    return (today && now.minute >= rule.on) || (yesterday && now.minute < rule.off);
}

inline bool scheduledOn(const RelayConfig& relay, const LocalMinute& now) {
    for (uint8_t i = 0; i < relay.count; ++i) {
        if (active(relay.rules[i], now)) return true;
    }
    return false;
}

}
