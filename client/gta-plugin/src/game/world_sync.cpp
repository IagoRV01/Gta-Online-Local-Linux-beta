#include "game/world_sync.h"
#include <cmath>
#include <string>
#include "common/log.h"
#include "common/world.h"
#include "engine/gta.h"
#include "engine/shv.h"

namespace world_sync {

using namespace proto;

static bool g_active = false, g_have = false, g_dirty = false, g_hold = false;
static WorldStateMsg g_state;
static std::string g_appliedWeather, g_appliedNext;
static int g_appliedFlags = -1;
// story mode values captured on entering YOU
static int g_storyH = 12, g_storyM = 0, g_storyS = 0;
static uint32_t g_storyWeather = 0;

static std::string WeatherName(uint32_t hash) {
    for (const auto &n : world::WeatherNames())
        if (gta::Joaat(n.c_str()) == hash) return n;
    return "";
}

void OnWorldState(const WorldStateMsg &m) {
    g_state = m;
    g_have = true;
    g_dirty = true;
}

void Begin() {
    g_storyH = N<int>(N_GET_CLOCK_HOURS);
    g_storyM = N<int>(N_GET_CLOCK_MINUTES);
    g_storyS = N<int>(N_GET_CLOCK_SECONDS);
    g_storyWeather = N<uint32_t>(N_GET_PREV_WEATHER_TYPE_HASH_NAME);
    g_active = true;
    g_appliedWeather.clear();
    g_appliedNext.clear();
    g_appliedFlags = -1;
    g_dirty = g_have;
    LOGI("world", "-", "story clock %02d:%02d kept (weather %s)", g_storyH, g_storyM, WeatherName(g_storyWeather).c_str());
}

void SetHold(bool hold) {
    if (hold == g_hold) return;
    g_hold = hold;
    if (!hold) { g_appliedWeather.clear(); g_appliedNext.clear(); g_appliedFlags = -1; g_dirty = g_have; }   // snap back to the server's
    LOGI("world", "-", "server world %s", hold ? "held (a job's own time and weather)" : "back");
}

void Frame() {
    if (!g_active || !g_have || !g_dirty || g_hold) return;
    g_dirty = false;
    const WorldStateMsg &m = g_state;
    // clock: GTA runs it at the same 2 s/minute as the server, so only correct real drift
    int local = N<int>(N_GET_CLOCK_HOURS) * 60 + N<int>(N_GET_CLOCK_MINUTES);
    int server = m.hour * 60 + m.minute;
    int drift = std::abs(local - server);
    if (drift > 720) drift = 1440 - drift;
    if (g_appliedFlags < 0 || drift > 2) N(N_SET_CLOCK_TIME, (int)m.hour, (int)m.minute, (int)m.second);
    if (g_appliedFlags < 0 || ((g_appliedFlags ^ m.flags) & kWorldClockFrozen)) N(N_PAUSE_CLOCK, (m.flags & kWorldClockFrozen) ? 1 : 0);
    if (g_appliedFlags < 0 || ((g_appliedFlags ^ m.flags) & kWorldBlackout)) N(N_SET_ARTIFICIAL_LIGHTS_STATE, (m.flags & kWorldBlackout) ? 1 : 0);
    // weather: snap on the first message, blend afterwards; follow a server-side transition
    if (world::ValidWeather(m.weather) && m.weather != g_appliedWeather) {
        if (g_appliedWeather.empty()) N(N_SET_WEATHER_TYPE_NOW_PERSIST, m.weather.c_str());
        else N(N_SET_WEATHER_TYPE_OVERTIME_PERSIST, m.weather.c_str(), 15.f);
        g_appliedWeather = m.weather;
        g_appliedNext = m.weather;
    }
    if (world::ValidWeather(m.nextWeather) && m.nextWeather != m.weather && m.nextWeather != g_appliedNext && m.transition > 0.f) {
        N(N_SET_WEATHER_TYPE_OVERTIME_PERSIST, m.nextWeather.c_str(), (1.f - m.transition) * 45.f + 1.f);
        g_appliedNext = m.nextWeather;
    }
    if (g_appliedFlags != m.flags || drift > 2)
        LOGI("world", "-", "server world %02d:%02d %s%s%s", m.hour, m.minute, m.weather.c_str(),
             (m.flags & kWorldClockFrozen) ? " frozen" : "", (m.flags & kWorldBlackout) ? " blackout" : "");
    g_appliedFlags = m.flags;
}

void End() {
    if (!g_active) return;
    g_active = false;
    g_hold = false;
    N(N_PAUSE_CLOCK, 0);
    N(N_SET_ARTIFICIAL_LIGHTS_STATE, 0);
    N(N_CLEAR_WEATHER_TYPE_PERSIST);
    N(N_CLEAR_OVERRIDE_WEATHER);
    N(N_SET_CLOCK_TIME, g_storyH, g_storyM, g_storyS);
    std::string w = WeatherName(g_storyWeather);
    if (!w.empty()) N(N_SET_WEATHER_TYPE_NOW, w.c_str());
    LOGI("world", "-", "story clock %02d:%02d and weather %s restored", g_storyH, g_storyM, w.c_str());
}

}  // namespace world_sync
