#include "common/vehicle.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace vehicle {

const char *StateName(State s) {
    switch (s) {
        case State::Stored: return "stored";
        case State::Active: return "active";
        case State::Destroyed: return "destroyed";
        case State::Impounded: return "impounded";
    }
    return "stored";
}

bool StateFromName(const std::string &n, State &out) {
    for (State s : {State::Stored, State::Active, State::Destroyed, State::Impounded})
        if (n == StateName(s)) { out = s; return true; }
    return false;
}

bool CanTransition(State from, State to) {
    switch (from) {
        case State::Stored: return to == State::Active;                                   // call it out
        case State::Active: return to == State::Active || to == State::Stored || to == State::Destroyed || to == State::Impounded;
        case State::Destroyed: return to == State::Stored;                                // insurance claim
        case State::Impounded: return to == State::Stored;                                // paid release
    }
    return false;
}

bool ValidPlate(const std::string &p) {
    if (p.empty() || p.size() > 8) return false;
    for (unsigned char c : p)
        if (!(std::isupper(c) || std::isdigit(c) || c == ' ')) return false;
    return p.find_first_not_of(' ') != std::string::npos;
}

bool Validate(Personal &v, std::string &err) {
    if (v.model == 0) { err = "no model"; return false; }
    if (!ValidPlate(v.plate)) { err = "bad plate"; return false; }
    for (float f : {v.engineHealth, v.bodyHealth, v.x, v.y, v.z, v.heading})
        if (!std::isfinite(f)) { err = "non-finite number"; return false; }
    auto clampi = [](int &x, int lo, int hi) { x = std::clamp(x, lo, hi); };
    clampi(v.color1, 0, 255); clampi(v.color2, 0, 255); clampi(v.pearl, 0, 255); clampi(v.wheelColor, 0, 255);
    clampi(v.wheelType, 0, 12); clampi(v.windowTint, -1, 6); clampi(v.plateIndex, 0, 12); clampi(v.livery, -1, 64);
    for (int &m : v.mods) clampi(m, -1, 255);
    v.engineHealth = std::clamp(v.engineHealth, -4000.f, 1000.f);
    v.bodyHealth = std::clamp(v.bodyHealth, 0.f, 1000.f);
    v.x = std::clamp(v.x, -16000.f, 16000.f);
    v.y = std::clamp(v.y, -16000.f, 16000.f);
    v.z = std::clamp(v.z, -200.f, 2700.f);
    v.heading = std::fmod(std::fmod(v.heading, 360.f) + 360.f, 360.f);
    return true;
}

static json::Value N(double v) { return json::Value::MakeNumber(v); }

json::Value ToJson(const Personal &v) {
    json::Value o = json::Value::MakeObject();
    o.Set("uuid", json::Value::MakeString(v.uuid));
    o.Set("model", N(v.model));
    o.Set("plate", json::Value::MakeString(v.plate));
    json::Value col;
    col.type = json::Value::Array;
    col.arr = {N(v.color1), N(v.color2), N(v.pearl), N(v.wheelColor)};
    o.Set("colors", col);
    o.Set("wheelType", N(v.wheelType));
    o.Set("windowTint", N(v.windowTint));
    o.Set("plateIndex", N(v.plateIndex));
    o.Set("livery", N(v.livery));
    json::Value mods;
    mods.type = json::Value::Array;
    for (int m : v.mods) mods.arr.push_back(N(m));
    o.Set("mods", mods);
    o.Set("turbo", json::Value::MakeBool(v.turbo));
    o.Set("xenon", json::Value::MakeBool(v.xenon));
    o.Set("engineHealth", N(std::round(v.engineHealth)));
    o.Set("bodyHealth", N(std::round(v.bodyHealth)));
    json::Value pos;
    pos.type = json::Value::Array;
    pos.arr = {N(std::round(v.x * 100) / 100), N(std::round(v.y * 100) / 100), N(std::round(v.z * 100) / 100), N(std::round(v.heading * 10) / 10)};
    o.Set("position", pos);
    o.Set("state", json::Value::MakeString(StateName(v.state)));
    return o;
}

bool FromJson(const json::Value &j, Personal &v, std::string &err) {
    if (j.type != json::Value::Object) { err = "not an object"; return false; }
    v = Personal{};
    v.uuid = j.Str("uuid", "");
    v.model = (uint32_t)j.Num("model", 0);
    v.plate = j.Str("plate", "");
    if (auto *c = j.Find("colors"); c && c->arr.size() == 4) {
        v.color1 = (int)c->arr[0].num; v.color2 = (int)c->arr[1].num; v.pearl = (int)c->arr[2].num; v.wheelColor = (int)c->arr[3].num;
    }
    v.wheelType = (int)j.Num("wheelType", 0);
    v.windowTint = (int)j.Num("windowTint", -1);
    v.plateIndex = (int)j.Num("plateIndex", 0);
    v.livery = (int)j.Num("livery", -1);
    if (auto *m = j.Find("mods"))
        for (size_t i = 0; i < m->arr.size() && i < v.mods.size(); ++i) v.mods[i] = (int)m->arr[i].num;
    v.turbo = j.Flag("turbo", false);
    v.xenon = j.Flag("xenon", false);
    v.engineHealth = (float)j.Num("engineHealth", 1000);
    v.bodyHealth = (float)j.Num("bodyHealth", 1000);
    if (auto *p = j.Find("position"); p && p->arr.size() == 4) {
        v.x = (float)p->arr[0].num; v.y = (float)p->arr[1].num; v.z = (float)p->arr[2].num; v.heading = (float)p->arr[3].num;
    }
    if (!StateFromName(j.Str("state", "stored"), v.state)) { err = "bad state"; return false; }
    return Validate(v, err);
}

}  // namespace vehicle
