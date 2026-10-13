#include "game/appearance.h"
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"

namespace appearance {

using namespace character;

// Weapons checked when capturing the loadout (story-mode set + common DLC weapons). Unknown hashes
// on older builds just report "not owned".
static const char *kWeapons[] = {
    "WEAPON_KNIFE", "WEAPON_NIGHTSTICK", "WEAPON_HAMMER", "WEAPON_BAT", "WEAPON_GOLFCLUB", "WEAPON_CROWBAR", "WEAPON_BOTTLE",
    "WEAPON_DAGGER", "WEAPON_HATCHET", "WEAPON_KNUCKLE", "WEAPON_MACHETE", "WEAPON_FLASHLIGHT", "WEAPON_SWITCHBLADE",
    "WEAPON_POOLCUE", "WEAPON_WRENCH", "WEAPON_BATTLEAXE", "WEAPON_STONE_HATCHET",
    "WEAPON_PISTOL", "WEAPON_PISTOL_MK2", "WEAPON_COMBATPISTOL", "WEAPON_APPISTOL", "WEAPON_STUNGUN", "WEAPON_PISTOL50",
    "WEAPON_SNSPISTOL", "WEAPON_HEAVYPISTOL", "WEAPON_VINTAGEPISTOL", "WEAPON_FLAREGUN", "WEAPON_MARKSMANPISTOL",
    "WEAPON_REVOLVER", "WEAPON_DOUBLEACTION", "WEAPON_CERAMICPISTOL", "WEAPON_NAVYREVOLVER",
    "WEAPON_MICROSMG", "WEAPON_SMG", "WEAPON_SMG_MK2", "WEAPON_ASSAULTSMG", "WEAPON_COMBATPDW", "WEAPON_MACHINEPISTOL",
    "WEAPON_MINISMG", "WEAPON_GUSENBERG",
    "WEAPON_PUMPSHOTGUN", "WEAPON_PUMPSHOTGUN_MK2", "WEAPON_SAWNOFFSHOTGUN", "WEAPON_ASSAULTSHOTGUN", "WEAPON_BULLPUPSHOTGUN",
    "WEAPON_MUSKET", "WEAPON_HEAVYSHOTGUN", "WEAPON_DBSHOTGUN", "WEAPON_AUTOSHOTGUN", "WEAPON_COMBATSHOTGUN",
    "WEAPON_ASSAULTRIFLE", "WEAPON_ASSAULTRIFLE_MK2", "WEAPON_CARBINERIFLE", "WEAPON_CARBINERIFLE_MK2", "WEAPON_ADVANCEDRIFLE",
    "WEAPON_SPECIALCARBINE", "WEAPON_SPECIALCARBINE_MK2", "WEAPON_BULLPUPRIFLE", "WEAPON_BULLPUPRIFLE_MK2", "WEAPON_COMPACTRIFLE",
    "WEAPON_MILITARYRIFLE", "WEAPON_HEAVYRIFLE", "WEAPON_TACTICALRIFLE",
    "WEAPON_MG", "WEAPON_COMBATMG", "WEAPON_COMBATMG_MK2",
    "WEAPON_SNIPERRIFLE", "WEAPON_HEAVYSNIPER", "WEAPON_HEAVYSNIPER_MK2", "WEAPON_MARKSMANRIFLE", "WEAPON_MARKSMANRIFLE_MK2",
    "WEAPON_PRECISIONRIFLE",
    "WEAPON_RPG", "WEAPON_GRENADELAUNCHER", "WEAPON_MINIGUN", "WEAPON_FIREWORK", "WEAPON_RAILGUN", "WEAPON_HOMINGLAUNCHER",
    "WEAPON_COMPACTLAUNCHER",
    "WEAPON_GRENADE", "WEAPON_BZGAS", "WEAPON_MOLOTOV", "WEAPON_STICKYBOMB", "WEAPON_PROXMINE", "WEAPON_PIPEBOMB",
    "WEAPON_SMOKEGRENADE", "WEAPON_FLARE", "WEAPON_SNOWBALL", "WEAPON_BALL",
    "WEAPON_PETROLCAN", "WEAPON_FIREEXTINGUISHER", "GADGET_PARACHUTE",
};

void ApplyLook(int ped, const Character &c) {
    const auto &b = c.blend;
    N(N_SET_PED_HEAD_BLEND_DATA, ped, b.shapeMother, b.shapeFather, b.shapeExtra, b.skinMother, b.skinFather, b.skinExtra,
      b.shapeMix, b.skinMix, b.extraMix, 0);
    for (int i = 0; i < kNumFaceFeatures; ++i) N(N_SET_PED_MICRO_MORPH, ped, i, c.face[i]);
    N(N_SET_HEAD_BLEND_EYE_COLOR, ped, c.eyeColor);

    for (int i = 0; i < kNumComponents; ++i) {
        const auto &v = c.components[i];
        int d = v.drawable, t = v.texture;
        if (d >= DrawableCount(ped, i)) d = 0;   // a DLC item missing on this install: fall back, keep the profile intact
        if (t >= TextureCount(ped, i, d)) t = 0;
        N(N_SET_PED_COMPONENT_VARIATION, ped, i, d, t, v.palette);
    }
    N(N_SET_PED_HAIR_TINT, ped, c.hairColor, c.hairHighlight);
    for (int i = 0; i < kNumOverlays; ++i) {
        const auto &o = c.overlays[i];
        N(N_SET_PED_HEAD_OVERLAY, ped, i, o.index, o.opacity);
        // colour type: 1 = hair colours (beard, brows, chest hair), 2 = makeup colours (blush, lipstick)
        int type = (i == FacialHair || i == Eyebrows || i == ChestHair) ? 1 : (i == Blush || i == Lipstick) ? 2 : 0;
        if (type) N(N_SET_PED_HEAD_OVERLAY_TINT, ped, i, type, o.color, o.color2);
    }
    for (int a = 0; a < kNumProps; ++a) {
        const auto &p = c.props[a];
        if (p.drawable < 0 || p.drawable >= PropCount(ped, a)) N(N_CLEAR_PED_PROP, ped, a, 0);
        else N(N_SET_PED_PROP_INDEX, ped, a, p.drawable, p.texture, 1, 0);
    }
    N(N_CLEAR_PED_DECORATIONS, ped);
    for (const auto &t : c.tattoos) N(N_ADD_PED_DECORATION_FROM_HASHES, ped, t.collection, t.overlay);
}

void ApplyState(int ped, const Character &c) {
    N(N_SET_PED_MAX_HEALTH, ped, 200);
    int hp = c.health <= 100 ? 200 : c.health;   // never spawn dead: a dead save comes back at full health
    N(N_SET_ENTITY_HEALTH, ped, hp, 0, 0);
    N(N_SET_PED_ARMOUR, ped, c.armour);
    GiveWeapons(ped, c.weapons);
}

void GiveWeapons(int ped, const std::vector<Weapon> &ws) {
    N(N_REMOVE_ALL_PED_WEAPONS, ped, 1);
    for (const auto &w : ws) N(N_GIVE_WEAPON_TO_PED, ped, w.hash, w.ammo, 0, 0);
    N(N_SET_CURRENT_PED_WEAPON, ped, gta::Joaat("WEAPON_UNARMED"), 1);
}

std::vector<Weapon> CaptureWeapons(int ped) {
    std::vector<Weapon> out;
    for (const char *name : kWeapons) {
        uint32_t h = gta::Joaat(name);
        if (NB(N<int>(N_HAS_PED_GOT_WEAPON, ped, h, 0))) out.push_back({h, N<int>(N_GET_AMMO_IN_PED_WEAPON, ped, h)});
    }
    return out;
}

void CaptureState(int ped, Character &c) {
    c.health = gta::Health(ped);
    c.armour = N<int>(N_GET_PED_ARMOUR, ped);
    c.weapons = CaptureWeapons(ped);
    gta::Vec3 p = gta::Pos(ped);
    c.pos = {p.x, p.y, p.z - 1.0f, gta::Heading(ped)};   // entity coords are at the pelvis; store the feet
}

int DrawableCount(int ped, int comp) { return N<int>(N_GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS, ped, comp); }
int TextureCount(int ped, int comp, int d) { return N<int>(N_GET_NUMBER_OF_PED_TEXTURE_VARIATIONS, ped, comp, d); }
int PropCount(int ped, int a) { return N<int>(N_GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS, ped, a); }
int PropTextureCount(int ped, int a, int d) { return N<int>(N_GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS, ped, a, d); }
int OverlayCount(int o) { return N<int>(N_GET_PED_HEAD_OVERLAY_NUM, o); }
int HairTintCount() { return N<int>(N_GET_NUM_PED_HAIR_TINTS); }

}  // namespace appearance
