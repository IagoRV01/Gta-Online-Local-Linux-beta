#include "common/interp.h"
#include <cmath>

namespace interp {

float LerpHeading(float a, float b, float t) {
    float d = std::fmod(b - a + 540.f, 360.f) - 180.f;   // -180..180
    float r = std::fmod(a + d * t + 360.f, 360.f);
    return r < 0 ? r + 360.f : r;
}

Quat Nlerp(Quat a, Quat b, float t) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
    Quat q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1e-6f) return a;
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

void Buffer::Push(const Sample &s) {
    if (!samples_.empty() && s.t <= samples_.back().t) return;
    samples_.push_back(s);
    while (samples_.size() > kMax) samples_.pop_front();
}

Result Buffer::At(double t, double maxEx) const {
    Result r;
    if (samples_.empty()) return r;
    r.valid = true;
    if (t <= samples_.front().t) { r.s = samples_.front(); return r; }
    for (size_t i = 1; i < samples_.size(); ++i) {
        const Sample &a = samples_[i - 1], &b = samples_[i];
        if (t <= b.t) {
            float k = (float)((t - a.t) / (b.t - a.t));
            r.s = b;
            r.s.t = t;
            r.s.pos = {a.pos.x + (b.pos.x - a.pos.x) * k, a.pos.y + (b.pos.y - a.pos.y) * k, a.pos.z + (b.pos.z - a.pos.z) * k};
            r.s.vel = {a.vel.x + (b.vel.x - a.vel.x) * k, a.vel.y + (b.vel.y - a.vel.y) * k, a.vel.z + (b.vel.z - a.vel.z) * k};
            r.s.heading = LerpHeading(a.heading, b.heading, k);
            r.s.rot = Nlerp(a.rot, b.rot, k);
            return r;
        }
    }
    const Sample &last = samples_.back();
    double dt = t - last.t;
    r.extrapolated = true;
    if (dt > maxEx) { r.stale = true; dt = maxEx; }
    r.s = last;
    r.s.t = t;
    r.s.pos = {last.pos.x + last.vel.x * (float)dt, last.pos.y + last.vel.y * (float)dt, last.pos.z + last.vel.z * (float)dt};
    return r;
}

void Clock::OnSnapshot(double serverTime, double localTime) {
    double sample = serverTime - localTime;
    if (!synced_ || std::fabs(sample - offset_) > 1.0) { offset_ = sample; synced_ = true; return; }   // first or big jump: snap
    offset_ += (sample - offset_) * 0.05;
}

}  // namespace interp
