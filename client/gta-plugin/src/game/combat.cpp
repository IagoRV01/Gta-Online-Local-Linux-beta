#include "game/combat.h"
#include <windows.h>
#include <cmath>
#include <string>
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/net_client.h"
#include "game/remote.h"

namespace combat {

using namespace proto;

static unsigned long long g_lastShotSent = 0;
static uint16_t g_lastAttacker = 0;
static uint32_t g_lastAttackerWeapon = 0;
static unsigned long long g_lastAttackedAt = 0;
static uint32_t g_lastFired = 0;
static unsigned long long g_lastFiredAt = 0;

Vec3 AimPoint() {
    NVec3 c = N<NVec3>(N_GET_FINAL_RENDERED_CAM_COORD);
    NVec3 r = N<NVec3>(N_GET_FINAL_RENDERED_CAM_ROT, 2);
    float pitch = r.x * 3.14159265f / 180.f, yaw = r.z * 3.14159265f / 180.f;
    float dx = -std::sin(yaw) * std::cos(pitch), dy = std::cos(yaw) * std::cos(pitch), dz = std::sin(pitch);
    const float range = 80.f;
    return {c.x + dx * range, c.y + dy * range, c.z + dz * range};
}

static void SendShot(int me) {
    unsigned long long now = GetTickCount64();
    if (now - g_lastShotSent < 66) return;   // <= 15 shot events/s: enough for tracers and sound
    g_lastShotSent = now;
    ShotMsg m;
    m.weapon = N<uint32_t>(N_GET_SELECTED_PED_WEAPON, me);
    int wobj = N<int>(N_GET_CURRENT_PED_WEAPON_ENTITY_INDEX, me, 0);
    gta::Vec3 o = gta::Exists(wobj) ? gta::Pos(wobj) : gta::Pos(me);
    m.origin = {o.x, o.y, o.z};
    NVec3 hit{};
    if (NB(N<int>(N_GET_PED_LAST_WEAPON_IMPACT_COORD, me, &hit))) m.target = {hit.x, hit.y, hit.z};
    else m.target = AimPoint();
    wire::Writer w;
    Put(w, m);
    net::SendEvent(w);
}

void Frame(int me) {
    if (!gta::Dead(me) && NB(N<int>(N_IS_PED_SHOOTING, me))) {
        g_lastFired = N<uint32_t>(N_GET_SELECTED_PED_WEAPON, me);
        g_lastFiredAt = GetTickCount64();
        SendShot(me);
    }
}

uint32_t AttackWeapon(int me) {
    if (g_lastFired && GetTickCount64() - g_lastFiredAt < 3000) return g_lastFired;
    return N<uint32_t>(N_GET_SELECTED_PED_WEAPON, me);
}

void OnDamageApply(const DamageApplyMsg &m) {
    int me = gta::PlayerPed();
    if (gta::Dead(me)) return;
    // armour first, like a normal hit; the weapon hash keeps the death cause meaningful
    N(N_APPLY_DAMAGE_TO_PED, me, (int)m.amount, 1, 0, m.weapon);
    g_lastAttacker = m.attacker;
    g_lastAttackerWeapon = m.weapon;
    g_lastAttackedAt = GetTickCount64();
    LOGI("combat", "-", "hit by %s for %u (weapon 0x%08X) -> health %d", remote::NameOf(m.attacker).c_str(), m.amount, m.weapon, gta::Health(me));
}

void OnLocalDeath() {
    DeathMsg d;
    if (g_lastAttacker && GetTickCount64() - g_lastAttackedAt < 10000) { d.killer = g_lastAttacker; d.weapon = g_lastAttackerWeapon; }
    else d.weapon = N<uint32_t>(N_GET_PED_CAUSE_OF_DEATH, gta::PlayerPed());
    wire::Writer w;
    Put(w, d);
    net::SendEvent(w);
    g_lastAttacker = 0;
}

void OnKillFeed(const KillFeedMsg &m) {
    uint16_t self = net::SelfNetId();
    std::string victim = m.victim == self ? "You" : remote::NameOf(m.victim);
    if (!m.killer) {
        gta::Notify("~r~" + victim + "~s~ died.");
    } else {
        std::string killer = m.killer == self ? "You" : remote::NameOf(m.killer);
        gta::Notify("~o~" + killer + "~s~ killed ~b~" + victim + "~s~.");
    }
    LOGI("combat", "-", "kill feed: victim %u killer %u weapon 0x%08X", m.victim, m.killer, m.weapon);
}

void Reset() { g_lastAttacker = 0; }

}  // namespace combat
