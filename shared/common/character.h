// The custom ("YOU") character: appearance + state, and its JSON form.
// One definition shared by the GTA client (apply/capture on the ped, local cache) and the Windows
// server (database, validation). The server is the authority; Validate() is what it runs on every
// profile a client uploads, and the client runs it too before applying anything to a ped.
// Pure C++: no GTA calls here (see client/gta-plugin/src/game/appearance.cpp for that).
#pragma once
#include <array>
#include <map>
#include <cstdint>
#include <string>
#include <vector>
#include "common/json.h"

namespace character {

constexpr int kSchemaVersion = 1;
constexpr int kNumFaceFeatures = 20;   // SET_PED_MICRO_MORPH 0..19
constexpr int kNumOverlays = 13;       // SET_PED_HEAD_OVERLAY 0..12
constexpr int kNumComponents = 12;     // SET_PED_COMPONENT_VARIATION 0..11
constexpr int kNumProps = 8;           // SET_PED_PROP_INDEX anchors 0..7 (0 hat, 1 glasses, 2 ears, 6 watch, 7 bracelet)
constexpr int kNoOverlay = 255;        // overlay index meaning "none"

// Overlay ids (rage::ePedHeadOverlay order).
enum Overlay { Blemishes, FacialHair, Eyebrows, Ageing, Makeup, Blush, Complexion, SunDamage, Lipstick, Moles, ChestHair, BodyBlemishes, AddBodyBlemishes };
// Component ids.
enum Component { Head, Mask, Hair, Torso, Legs, Bag, Feet, Accessory, Undershirt, Armor, Decals, Tops };   // Torso = arms/gloves

struct HeadBlend {
    int shapeMother = 21, shapeFather = 0, shapeExtra = 0;   // "parents" (face shapes 0..45)
    int skinMother = 21, skinFather = 0, skinExtra = 0;      // skin tones 0..45
    float shapeMix = 0.5f, skinMix = 0.5f, extraMix = 0.f;   // 0 = mother, 1 = father
};
struct OverlaySlot { int index = kNoOverlay; float opacity = 1.f; int color = 0, color2 = 0; };
struct Variation { int drawable = 0, texture = 0, palette = 0; };
struct PropSlot { int drawable = -1, texture = 0; };   // -1 = nothing on that anchor
struct Tattoo { uint32_t collection = 0, overlay = 0; };
struct Weapon { uint32_t hash = 0; int ammo = 0; };
struct Position { float x = -1037.8f, y = -2737.6f, z = 20.2f, heading = 330.f; };   // LSIA arrivals (default spawn)

struct Character {
    int schema = kSchemaVersion;
    std::string uuid;            // stable id, assigned at creation (server-assigned once Phase 2 lands)
    std::string name;
    bool female = false;         // mp_f_freemode_01 when true, else mp_m_freemode_01
    HeadBlend blend;
    std::array<float, kNumFaceFeatures> face{};   // -1..1 each
    int eyeColor = 0;
    int hairColor = 0, hairHighlight = 0;
    std::array<OverlaySlot, kNumOverlays> overlays{};
    std::array<Variation, kNumComponents> components{};
    std::array<PropSlot, kNumProps> props{};
    std::vector<Tattoo> tattoos;

    // state
    int health = 200, armour = 0;            // freemode peds: 100 = dead, 200 = full
    std::vector<Weapon> weapons;
    int64_t money = 0;                       // wallet (cash on hand)
    int64_t bank = 0;                        // Maze Bank account
    Position pos;
    std::vector<std::string> inventory;      // reserved (Phase 8)
    // Other mods' per-character state (e.g. "gorionline": properties, businesses, heists, RP), kept
    // with the profile on the server. Opaque text; keys [a-z0-9_.-]{1,32}, see kMaxModData*.
    std::map<std::string, std::string> modData;
    int64_t playSeconds = 0;
    int deaths = 0;
    uint32_t revision = 0;                   // bumped on every save; the server rejects stale revisions
    std::string updatedAt;                   // ISO-8601, informational
};

constexpr size_t kMaxModDataKeys = 8, kMaxModDataValue = 24 * 1024;
bool ValidModKey(const std::string &key);

uint32_t ModelHash(const Character &c);      // joaat of the freemode model
Character MakeDefault(bool female);          // a valid starting look (clothes included)

// Clamps every field into its legal range and fixes what can be fixed (returns the list of fixes,
// empty when the profile was already clean). Returns false only when the profile is unusable
// (bad name, bad uuid, non-finite numbers) and must be rejected.
bool Validate(Character &c, std::vector<std::string> &problems);
bool ValidName(const std::string &name);
bool ValidUuid(const std::string &uuid);
std::string NewUuid();                       // random v4

json::Value ToJson(const Character &c);
// What other players need to draw this character: the look, no money/weapons/position.
Character AppearanceOnly(const Character &c);
bool FromJson(const json::Value &v, Character &out, std::string &err);   // also runs Validate

uint32_t Joaat(const char *s);

}  // namespace character
