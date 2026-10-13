// Shared world state owned by the server (Phase 7): clock, weather, blackout. Pure, host-testable.
// Clients only display it; the server advances it and persists it (world_state table).
#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace world {

constexpr int kMinutesPerDay = 24 * 60;
constexpr int kDefaultMsPerGameMinute = 2000;   // GTA V's own clock rate (not changeable from scripts)

// Valid GTA V weather type names (SET_WEATHER_TYPE_*). Anything else is rejected.
bool ValidWeather(const std::string &name);
const std::vector<std::string> &WeatherNames();

class Clock {
public:
    void Set(int hour, int minute, int second = 0);
    void Advance(int64_t realMs);              // no-op while frozen
    int Hour() const { return (int)(minutes_ / 60) % 24; }
    int Minute() const { return (int)minutes_ % 60; }
    int Second() const { return (int)((minutes_ - (int64_t)minutes_) * 60); }
    double Minutes() const { return minutes_; }   // 0 .. 1440
    void SetMinutes(double m);
    bool frozen = false;
    int msPerGameMinute = kDefaultMsPerGameMinute;

private:
    double minutes_ = 12 * 60;
};

// Weather that changes every `holdMinutes` real minutes (random within a range) to a plausible next
// type, with a `transitionMs` blend. `Set` forces a type (admin / GodEye) and restarts the hold.
class Weather {
public:
    explicit Weather(uint32_t seed = 1) : rng_(seed) {}
    bool Set(const std::string &name, int64_t nowMs);          // false if invalid
    void Advance(int64_t nowMs);                               // may start a transition
    const std::string &Current() const { return current_; }
    const std::string &Next() const { return next_; }
    float Transition(int64_t nowMs) const;                     // 0 = current, 1 = next
    bool automatic = true;
    int minHoldMs = 15 * 60 * 1000, maxHoldMs = 30 * 60 * 1000, transitionMs = 45 * 1000;

private:
    std::string PickNext();
    std::mt19937 rng_;
    std::string current_ = "CLEAR", next_ = "CLEAR";
    int64_t holdUntil_ = 0, transitionStart_ = -1;
};

}  // namespace world
