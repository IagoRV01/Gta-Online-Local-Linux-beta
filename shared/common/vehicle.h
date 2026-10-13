// Personal vehicles (Phase 8): what the server stores per owned car and how it changes state.
// Shared by the client (capture/apply on a GTA vehicle) and the server (validation, storage).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "common/json.h"

namespace vehicle {

constexpr int kNumMods = 50;            // GET_VEHICLE_MOD 0..49 (17-22 are toggles: turbo 18, xenon 22)
constexpr int kMaxPerCharacter = 10;

enum class State : uint8_t { Stored = 0, Active = 1, Destroyed = 2, Impounded = 3 };
const char *StateName(State s);         // "stored" | "active" | "destroyed" | "impounded"
bool StateFromName(const std::string &n, State &out);
// Allowed transitions; same-state updates are allowed for Active (position/health refresh).
bool CanTransition(State from, State to);

struct Personal {
    std::string uuid;                   // assigned by the server
    uint32_t model = 0;
    std::string plate;                  // 1..8 chars A-Z 0-9 space
    int color1 = 0, color2 = 0, pearl = 0, wheelColor = 0;
    int wheelType = 0, windowTint = -1, plateIndex = 0, livery = -1;
    std::array<int, kNumMods> mods{};   // -1 = stock
    bool turbo = false, xenon = false;
    float engineHealth = 1000.f, bodyHealth = 1000.f;
    float x = 0, y = 0, z = 0, heading = 0;
    State state = State::Stored;
    Personal() { mods.fill(-1); }
};

bool ValidPlate(const std::string &p);
// Clamps/fixes fields; false if unusable (no model, bad plate, non-finite numbers).
bool Validate(Personal &v, std::string &err);
json::Value ToJson(const Personal &v);
bool FromJson(const json::Value &j, Personal &out, std::string &err);   // also validates

}  // namespace vehicle
