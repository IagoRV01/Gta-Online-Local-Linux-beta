#include "common/world.h"
#include <algorithm>
#include <cmath>

namespace world {

const std::vector<std::string> &WeatherNames() {
    static const std::vector<std::string> names = {"EXTRASUNNY", "CLEAR", "CLOUDS", "OVERCAST", "SMOG", "FOGGY", "CLEARING", "RAIN",
                                                   "THUNDER", "NEUTRAL", "SNOW", "SNOWLIGHT", "BLIZZARD", "XMAS", "HALLOWEEN"};
    return names;
}

bool ValidWeather(const std::string &n) {
    const auto &v = WeatherNames();
    return std::find(v.begin(), v.end(), n) != v.end();
}

void Clock::Set(int h, int m, int s) {
    h = ((h % 24) + 24) % 24;
    m = std::clamp(m, 0, 59);
    s = std::clamp(s, 0, 59);
    minutes_ = h * 60 + m + s / 60.0;
}

void Clock::SetMinutes(double m) {
    if (!std::isfinite(m)) return;
    minutes_ = std::fmod(std::fmod(m, (double)kMinutesPerDay) + kMinutesPerDay, (double)kMinutesPerDay);
}

void Clock::Advance(int64_t realMs) {
    if (frozen || realMs <= 0 || msPerGameMinute <= 0) return;
    SetMinutes(minutes_ + (double)realMs / msPerGameMinute);
}

// Plausible successions (roughly the game's own cycle); weights favour fair weather.
std::string Weather::PickNext() {
    struct Option { const char *name; int weight; };
    static const std::vector<std::pair<std::string, std::vector<Option>>> table = {
        {"EXTRASUNNY", {{"CLEAR", 5}, {"EXTRASUNNY", 3}, {"CLOUDS", 2}, {"SMOG", 1}}},
        {"CLEAR", {{"EXTRASUNNY", 4}, {"CLOUDS", 4}, {"CLEAR", 2}, {"SMOG", 1}, {"FOGGY", 1}}},
        {"CLOUDS", {{"CLEAR", 4}, {"OVERCAST", 3}, {"CLOUDS", 2}}},
        {"OVERCAST", {{"CLOUDS", 3}, {"RAIN", 3}, {"CLEARING", 1}}},
        {"RAIN", {{"CLEARING", 4}, {"THUNDER", 1}, {"RAIN", 1}}},
        {"THUNDER", {{"RAIN", 2}, {"CLEARING", 3}}},
        {"CLEARING", {{"CLEAR", 4}, {"CLOUDS", 2}}},
        {"SMOG", {{"CLEAR", 3}, {"CLOUDS", 2}}},
        {"FOGGY", {{"CLEAR", 3}, {"CLOUDS", 2}}},
    };
    for (const auto &row : table) {
        if (row.first != current_) continue;
        int total = 0;
        for (const auto &o : row.second) total += o.weight;
        int pick = std::uniform_int_distribution<int>(0, total - 1)(rng_);
        for (const auto &o : row.second) { if (pick < o.weight) return o.name; pick -= o.weight; }
    }
    return "CLEAR";   // special types (snow, xmas...) fall back to clear when automatic
}

bool Weather::Set(const std::string &name, int64_t nowMs) {
    if (!ValidWeather(name)) return false;
    current_ = next_ = name;
    transitionStart_ = -1;
    holdUntil_ = nowMs + std::uniform_int_distribution<int>(minHoldMs, maxHoldMs)(rng_);
    return true;
}

void Weather::Advance(int64_t nowMs) {
    if (transitionStart_ >= 0) {
        if (nowMs - transitionStart_ >= transitionMs) {   // blend finished
            current_ = next_;
            transitionStart_ = -1;
            holdUntil_ = nowMs + std::uniform_int_distribution<int>(minHoldMs, maxHoldMs)(rng_);
        }
        return;
    }
    if (holdUntil_ == 0) holdUntil_ = nowMs + std::uniform_int_distribution<int>(minHoldMs, maxHoldMs)(rng_);
    if (!automatic || nowMs < holdUntil_) return;
    next_ = PickNext();
    if (next_ == current_) { holdUntil_ = nowMs + minHoldMs; return; }
    transitionStart_ = nowMs;
}

float Weather::Transition(int64_t nowMs) const {
    if (transitionStart_ < 0 || transitionMs <= 0) return 0.f;
    float t = (float)(nowMs - transitionStart_) / transitionMs;
    return t < 0 ? 0.f : t > 1 ? 1.f : t;
}

}  // namespace world
