#include <stdio.h>
#include <stdlib.h>
#include "../include/schedule.h"

using namespace natal;
unsigned long checks = 0;

void check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        fprintf(stderr, "FAIL: %s (check %lu)\n", description, checks);
        exit(1);
    }
}

int main() {
    uint16_t value = 999;
    check(parseMinute("00:00", value) && value == 0, "midnight");
    check(parseMinute("23:59", value) && value == 1439, "last minute");
    check(parseMinute("18:30", value) && value == 1110, "normal time");
    const char* invalid[] = {nullptr, "", "1", "18", "18:", "18:0", "24:00",
                             "23:60", "-1:00", "aa:bb", "12-30", "12:30:00", " 1:00", "1x:00"};
    for (const char* text : invalid) check(!parseMinute(text, value), "reject malformed time");

    Rule weekly;
    weekly.days = 1 << 1;
    weekly.on = 18 * 60;
    weekly.off = 23 * 60;
    for (uint8_t day = 0; day < 7; ++day) {
        for (uint16_t minute = 0; minute < 1440; ++minute) {
            LocalMinute now{day, 12, 11, minute};
            check(active(weekly, now) == (day == 1 && minute >= 1080 && minute < 1380),
                  "weekly start inclusive and end exclusive, all weekdays");
        }
    }
    weekly.on = 22 * 60;
    weekly.off = 6 * 60;
    for (uint8_t day = 0; day < 7; ++day) {
        for (uint16_t minute = 0; minute < 1440; ++minute) {
            LocalMinute now{day, 12, 11, minute};
            check(active(weekly, now) == ((day == 1 && minute >= 1320) || (day == 2 && minute < 360)),
                  "overnight belongs to selected start day");
        }
    }
    weekly.days = 1 << 6;
    check(active(weekly, {0, 1, 31, 120}), "Saturday to Sunday and month/year rollover");
    check(!active(weekly, {0, 1, 31, 360}), "overnight end excluded");
    weekly.enabled = false;
    check(!active(weekly, {6, 31, 30, 1400}), "disabled rule");
    weekly.enabled = true;
    weekly.off = weekly.on;
    check(!active(weekly, {6, 31, 30, 1400}), "equal times not a full day");

    Rule monthly;
    monthly.monthly = true;
    monthly.monthDay = 31;
    monthly.on = 1320;
    monthly.off = 360;
    check(active(monthly, {4, 31, 30, 1320}), "monthly starts on day 31");
    check(active(monthly, {5, 1, 31, 359}), "monthly carries into next month/year");
    check(!active(monthly, {5, 1, 31, 360}), "monthly end excluded");
    check(!active(monthly, {5, 1, 30, 120}), "day 31 does not exist in previous month");
    monthly.monthDay = 29;
    check(active(monthly, {5, 1, 29, 120}), "leap February carries into March");
    check(!active(monthly, {5, 1, 28, 120}), "non-leap February has no day 29");
    monthly.monthDay = 1;
    check(!active(monthly, {5, 1, 31, 120}), "early morning belongs to yesterday, not today");
    monthly.on = 0;
    monthly.off = 60;
    check(active(monthly, {5, 1, 31, 0}), "monthly starts at midnight");
    check(!active(monthly, {5, 2, 1, 0}), "monthly only on selected day");

    RelayConfig relays[RELAY_COUNT];
    relays[0].count = 2;
    relays[0].rules[0] = Rule{};
    relays[0].rules[0].on = 1080;
    relays[0].rules[0].off = 1200;
    relays[0].rules[1] = Rule{};
    relays[0].rules[1].on = 1140;
    relays[0].rules[1].off = 1380;
    for (uint16_t minute = 0; minute < 1440; ++minute) {
        LocalMinute now{1, 12, 11, minute};
        check(scheduledOn(relays[0], now) == (minute >= 1080 && minute < 1380),
              "overlapping periods form a union without an off gap");
        for (uint8_t i = 1; i < RELAY_COUNT; ++i) {
            check(!scheduledOn(relays[i], now), "independent relays and empty schedules stay off");
        }
    }
    relays[0].count = MAX_RULES;
    for (uint8_t i = 0; i < MAX_RULES; ++i) relays[0].rules[i].enabled = false;
    relays[0].rules[MAX_RULES - 1].enabled = true;
    check(scheduledOn(relays[0], {1, 12, 11, 1150}), "last of 16 periods is evaluated");
    printf("PASS: %lu schedule checks\n", checks);
    return 0;
}
