// Snapshot interpolation for remote players (pure, host-testable).
// Remote entities are rendered `delay` seconds in the past: between two received samples we lerp
// position and take the shortest-arc heading; past the newest sample we dead-reckon from its
// velocity for at most `maxExtrapolation` seconds, then hold.
#pragma once
#include <cstdint>
#include <deque>

namespace interp {

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
Quat Nlerp(Quat a, Quat b, float t);   // shortest-path normalised lerp (fine for 50 ms steps)

struct Sample {
    double t = 0;          // server time, seconds
    Vec3 pos, vel;
    float heading = 0;     // degrees
    Quat rot;              // vehicles: full orientation (peds use heading)
    uint8_t move = 0;      // proto::Move
    uint16_t flags = 0;
};

struct Result {
    Sample s;
    bool valid = false;
    bool extrapolated = false;
    bool stale = false;    // ran past maxExtrapolation (no fresh data)
};

float LerpHeading(float a, float b, float t);   // shortest arc, result in [0, 360)

class Buffer {
public:
    void Push(const Sample &s);           // ignores samples older than the newest one
    Result At(double t, double maxExtrapolation = 0.25) const;
    bool Empty() const { return samples_.empty(); }
    double NewestTime() const { return samples_.empty() ? 0 : samples_.back().t; }
    void Clear() { samples_.clear(); }

private:
    std::deque<Sample> samples_;          // ascending t, at most kMax
    static constexpr size_t kMax = 32;
};

// Maps local time to server time from snapshot timestamps (smoothed offset).
class Clock {
public:
    void OnSnapshot(double serverTime, double localTime);
    double ServerNow(double localTime) const { return localTime + offset_; }
    bool Synced() const { return synced_; }

private:
    double offset_ = 0;
    bool synced_ = false;
};

}  // namespace interp
