#include "common/character.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <random>

namespace character {

uint32_t Joaat(const char *s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        h += (uint32_t)std::tolower((unsigned char)*s);
        h += h << 10;
        h ^= h >> 6;
    }
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}

uint32_t ModelHash(const Character &c) { return Joaat(c.female ? "mp_f_freemode_01" : "mp_m_freemode_01"); }

Character MakeDefault(bool female) {
    Character c;
    c.female = female;
    c.name = female ? "Player" : "Player";
    c.blend = female ? HeadBlend{21, 0, 0, 21, 0, 0, 0.2f, 0.4f, 0.f} : HeadBlend{21, 0, 0, 21, 0, 0, 0.8f, 0.6f, 0.f};
    c.eyeColor = 3;
    c.hairColor = 2;
    c.hairHighlight = 2;
    c.overlays[Eyebrows] = {female ? 1 : 0, 1.f, 2, 2};
    // A plain casual outfit; the creator lets the player change every slot.
    auto set = [&](int slot, int d, int t = 0) { c.components[slot] = {d, t, 0}; };
    if (female) { set(Hair, 4); set(Torso, 0); set(Legs, 0); set(Feet, 1); set(Undershirt, 2); set(Tops, 0); }
    else        { set(Hair, 2); set(Torso, 0); set(Legs, 0); set(Feet, 1); set(Undershirt, 15); set(Tops, 0); }
    c.weapons = {{Joaat("WEAPON_PISTOL"), 120}};
    return c;
}

bool ValidModKey(const std::string &k) {
    if (k.empty() || k.size() > 32) return false;
    for (char ch : k)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-')) return false;
    return true;
}

bool ValidName(const std::string &n) {
    if (n.size() < 2 || n.size() > 24) return false;
    for (unsigned char ch : n)
        if (!(std::isalnum(ch) || ch == ' ' || ch == '_' || ch == '-' || ch == '.')) return false;
    return n.front() != ' ' && n.back() != ' ';
}

bool ValidUuid(const std::string &u) {
    if (u.size() != 36) return false;
    for (size_t i = 0; i < u.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (u[i] != '-') return false; }
        else if (!std::isxdigit((unsigned char)u[i])) return false;
    }
    return true;
}

std::string NewUuid() {
    std::random_device rd;
    std::mt19937_64 rng(((uint64_t)rd() << 32) ^ rd());
    uint64_t a = rng(), b = rng();
    a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;   // version 4
    b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;   // variant 10
    char s[40];
    std::snprintf(s, sizeof s, "%08x-%04x-%04x-%04x-%012llx", (unsigned)(a >> 32), (unsigned)((a >> 16) & 0xFFFF),
                  (unsigned)(a & 0xFFFF), (unsigned)(b >> 48), (unsigned long long)(b & 0xFFFFFFFFFFFFull));
    return s;
}

namespace {
struct Fixer {
    std::vector<std::string> &out;
    bool fatal = false;
    template <typename T> void Clamp(T &v, T lo, T hi, const char *what) {
        if (v < lo || v > hi) {
            char m[96];
            std::snprintf(m, sizeof m, "%s out of range (%g), clamped", what, (double)v);
            out.push_back(m);
            v = v < lo ? lo : hi;
        }
    }
    void Finite(float &v, const char *what) {
        if (!std::isfinite(v)) { out.push_back(std::string(what) + " not finite"); fatal = true; v = 0; }
    }
};
}  // namespace

bool Validate(Character &c, std::vector<std::string> &problems) {
    Fixer f{problems};
    if (c.schema != kSchemaVersion) { problems.push_back("unknown schema " + std::to_string(c.schema)); f.fatal = true; }
    if (!ValidUuid(c.uuid)) { problems.push_back("bad uuid"); f.fatal = true; }
    if (!ValidName(c.name)) { problems.push_back("bad name"); f.fatal = true; }
    auto &b = c.blend;
    f.Clamp(b.shapeMother, 0, 45, "shapeMother"); f.Clamp(b.shapeFather, 0, 45, "shapeFather"); f.Clamp(b.shapeExtra, 0, 45, "shapeExtra");
    f.Clamp(b.skinMother, 0, 45, "skinMother");   f.Clamp(b.skinFather, 0, 45, "skinFather");   f.Clamp(b.skinExtra, 0, 45, "skinExtra");
    f.Finite(b.shapeMix, "shapeMix"); f.Finite(b.skinMix, "skinMix"); f.Finite(b.extraMix, "extraMix");
    f.Clamp(b.shapeMix, 0.f, 1.f, "shapeMix"); f.Clamp(b.skinMix, 0.f, 1.f, "skinMix"); f.Clamp(b.extraMix, 0.f, 1.f, "extraMix");
    for (auto &x : c.face) { f.Finite(x, "face"); f.Clamp(x, -1.f, 1.f, "face"); }
    f.Clamp(c.eyeColor, 0, 31, "eyeColor");
    f.Clamp(c.hairColor, 0, 63, "hairColor");
    f.Clamp(c.hairHighlight, 0, 63, "hairHighlight");
    for (auto &o : c.overlays) {
        if (o.index != kNoOverlay) f.Clamp(o.index, 0, 254, "overlay.index");
        f.Finite(o.opacity, "overlay.opacity");
        f.Clamp(o.opacity, 0.f, 1.f, "overlay.opacity");
        f.Clamp(o.color, 0, 63, "overlay.color");
        f.Clamp(o.color2, 0, 63, "overlay.color2");
    }
    // Upper bounds are generous: the exact counts depend on installed DLCs and are checked on the
    // ped by the client (GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS) before applying.
    for (auto &v : c.components) { f.Clamp(v.drawable, 0, 1023, "component.drawable"); f.Clamp(v.texture, 0, 63, "component.texture"); f.Clamp(v.palette, 0, 3, "component.palette"); }
    for (auto &p : c.props) { f.Clamp(p.drawable, -1, 1023, "prop.drawable"); f.Clamp(p.texture, 0, 63, "prop.texture"); }
    if (c.tattoos.size() > 128) { problems.push_back("too many tattoos, truncated"); c.tattoos.resize(128); }
    f.Clamp(c.health, 0, 1000, "health");
    f.Clamp(c.armour, 0, 100, "armour");
    if (c.weapons.size() > 64) { problems.push_back("too many weapons, truncated"); c.weapons.resize(64); }
    for (auto &w : c.weapons) f.Clamp(w.ammo, 0, 9999, "weapon.ammo");
    f.Clamp<int64_t>(c.money, 0, 2000000000000ll, "money");
    f.Clamp<int64_t>(c.bank, 0, 2000000000000ll, "bank");
    f.Finite(c.pos.x, "pos.x"); f.Finite(c.pos.y, "pos.y"); f.Finite(c.pos.z, "pos.z"); f.Finite(c.pos.heading, "pos.heading");
    f.Clamp(c.pos.x, -16000.f, 16000.f, "pos.x");
    f.Clamp(c.pos.y, -16000.f, 16000.f, "pos.y");
    f.Clamp(c.pos.z, -200.f, 2700.f, "pos.z");
    c.pos.heading = std::fmod(std::fmod(c.pos.heading, 360.f) + 360.f, 360.f);
    if (c.inventory.size() > 256) { problems.push_back("inventory truncated"); c.inventory.resize(256); }
    for (auto it = c.modData.begin(); it != c.modData.end();) {
        if (!ValidModKey(it->first) || it->second.size() > kMaxModDataValue || std::distance(c.modData.begin(), it) >= (long)kMaxModDataKeys) {
            problems.push_back("mod data '" + it->first.substr(0, 32) + "' dropped");
            it = c.modData.erase(it);
        } else ++it;
    }
    if (c.playSeconds < 0) c.playSeconds = 0;
    if (c.deaths < 0) c.deaths = 0;
    return !f.fatal;
}

// ---------------------------------------------------------------- JSON

static json::Value Num(double v) { return json::Value::MakeNumber(v); }
static json::Value Arr() { json::Value a; a.type = json::Value::Array; return a; }
static json::Value Obj() { return json::Value::MakeObject(); }

Character AppearanceOnly(const Character &c) {
    Character a = c;
    a.weapons.clear();
    a.money = 0;
    a.bank = 0;
    a.inventory.clear();
    a.modData.clear();
    a.pos = Position{};
    a.playSeconds = 0;
    a.deaths = 0;
    a.health = 200;
    a.armour = 0;
    a.revision = 0;
    a.updatedAt.clear();
    return a;
}

json::Value ToJson(const Character &c) {
    json::Value r = Obj();
    r.Set("schema", Num(c.schema));
    r.Set("uuid", json::Value::MakeString(c.uuid));
    r.Set("name", json::Value::MakeString(c.name));
    r.Set("model", json::Value::MakeString(c.female ? "mp_f_freemode_01" : "mp_m_freemode_01"));
    json::Value b = Obj();
    b.Set("shapeMother", Num(c.blend.shapeMother)); b.Set("shapeFather", Num(c.blend.shapeFather)); b.Set("shapeExtra", Num(c.blend.shapeExtra));
    b.Set("skinMother", Num(c.blend.skinMother));   b.Set("skinFather", Num(c.blend.skinFather));   b.Set("skinExtra", Num(c.blend.skinExtra));
    b.Set("shapeMix", Num(c.blend.shapeMix)); b.Set("skinMix", Num(c.blend.skinMix)); b.Set("extraMix", Num(c.blend.extraMix));
    r.Set("headBlend", b);
    json::Value face = Arr();
    for (float x : c.face) face.arr.push_back(Num(std::round(x * 1000) / 1000));
    r.Set("faceFeatures", face);
    r.Set("eyeColor", Num(c.eyeColor));
    json::Value hair = Obj();
    hair.Set("color", Num(c.hairColor)); hair.Set("highlight", Num(c.hairHighlight));
    r.Set("hair", hair);
    json::Value ov = Arr();
    for (auto &o : c.overlays) {
        json::Value x = Obj();
        x.Set("index", Num(o.index)); x.Set("opacity", Num(std::round(o.opacity * 1000) / 1000)); x.Set("color", Num(o.color)); x.Set("color2", Num(o.color2));
        ov.arr.push_back(x);
    }
    r.Set("overlays", ov);
    json::Value comps = Arr();
    for (auto &v : c.components) { json::Value x = Arr(); x.arr = {Num(v.drawable), Num(v.texture), Num(v.palette)}; comps.arr.push_back(x); }
    r.Set("components", comps);
    json::Value props = Arr();
    for (auto &p : c.props) { json::Value x = Arr(); x.arr = {Num(p.drawable), Num(p.texture)}; props.arr.push_back(x); }
    r.Set("props", props);
    json::Value tats = Arr();
    for (auto &t : c.tattoos) { json::Value x = Arr(); x.arr = {Num(t.collection), Num(t.overlay)}; tats.arr.push_back(x); }
    r.Set("tattoos", tats);
    json::Value st = Obj();
    st.Set("health", Num(c.health)); st.Set("armour", Num(c.armour)); st.Set("money", Num((double)c.money)); st.Set("bank", Num((double)c.bank));
    st.Set("playSeconds", Num((double)c.playSeconds)); st.Set("deaths", Num(c.deaths));
    r.Set("state", st);
    json::Value w = Arr();
    for (auto &x : c.weapons) { json::Value e = Arr(); e.arr = {Num(x.hash), Num(x.ammo)}; w.arr.push_back(e); }
    r.Set("weapons", w);
    json::Value p = Obj();
    p.Set("x", Num(std::round(c.pos.x * 100) / 100)); p.Set("y", Num(std::round(c.pos.y * 100) / 100));
    p.Set("z", Num(std::round(c.pos.z * 100) / 100)); p.Set("heading", Num(std::round(c.pos.heading * 10) / 10));
    r.Set("position", p);
    json::Value inv = Arr();
    for (auto &s : c.inventory) inv.arr.push_back(json::Value::MakeString(s));
    r.Set("inventory", inv);
    json::Value mods = Obj();
    for (auto &kv : c.modData) mods.Set(kv.first, json::Value::MakeString(kv.second));
    r.Set("mods", mods);
    r.Set("revision", Num(c.revision));
    r.Set("updatedAt", json::Value::MakeString(c.updatedAt));
    return r;
}

static int I(const json::Value *v, int def) { return v && v->type == json::Value::Number ? (int)v->num : def; }
static float F(const json::Value *v, float def) { return v && v->type == json::Value::Number ? (float)v->num : def; }

bool FromJson(const json::Value &v, Character &c, std::string &err) {
    if (v.type != json::Value::Object) { err = "profile is not an object"; return false; }
    c = Character{};
    c.schema = I(v.Find("schema"), 0);
    c.uuid = v.Str("uuid", "");
    c.name = v.Str("name", "");
    c.female = v.Str("model", "mp_m_freemode_01") == "mp_f_freemode_01";
    if (auto *b = v.Find("headBlend")) {
        c.blend.shapeMother = I(b->Find("shapeMother"), 21); c.blend.shapeFather = I(b->Find("shapeFather"), 0); c.blend.shapeExtra = I(b->Find("shapeExtra"), 0);
        c.blend.skinMother = I(b->Find("skinMother"), 21);   c.blend.skinFather = I(b->Find("skinFather"), 0);   c.blend.skinExtra = I(b->Find("skinExtra"), 0);
        c.blend.shapeMix = F(b->Find("shapeMix"), .5f); c.blend.skinMix = F(b->Find("skinMix"), .5f); c.blend.extraMix = F(b->Find("extraMix"), 0);
    }
    if (auto *a = v.Find("faceFeatures"))
        for (size_t i = 0; i < a->arr.size() && i < c.face.size(); ++i) c.face[i] = F(&a->arr[i], 0);
    c.eyeColor = I(v.Find("eyeColor"), 0);
    if (auto *h = v.Find("hair")) { c.hairColor = I(h->Find("color"), 0); c.hairHighlight = I(h->Find("highlight"), 0); }
    if (auto *a = v.Find("overlays"))
        for (size_t i = 0; i < a->arr.size() && i < c.overlays.size(); ++i) {
            auto &x = a->arr[i];
            c.overlays[i] = {I(x.Find("index"), kNoOverlay), F(x.Find("opacity"), 1), I(x.Find("color"), 0), I(x.Find("color2"), 0)};
        }
    if (auto *a = v.Find("components"))
        for (size_t i = 0; i < a->arr.size() && i < c.components.size(); ++i) {
            auto &x = a->arr[i].arr;
            if (x.size() >= 2) c.components[i] = {I(&x[0], 0), I(&x[1], 0), x.size() > 2 ? I(&x[2], 0) : 0};
        }
    if (auto *a = v.Find("props"))
        for (size_t i = 0; i < a->arr.size() && i < c.props.size(); ++i) {
            auto &x = a->arr[i].arr;
            if (x.size() >= 2) c.props[i] = {I(&x[0], -1), I(&x[1], 0)};
        }
    if (auto *a = v.Find("tattoos"))
        for (auto &x : a->arr)
            if (x.arr.size() == 2) c.tattoos.push_back({(uint32_t)x.arr[0].num, (uint32_t)x.arr[1].num});
    if (auto *s = v.Find("state")) {
        c.health = I(s->Find("health"), 200); c.armour = I(s->Find("armour"), 0);
        c.money = (int64_t)s->Num("money", 0); c.bank = (int64_t)s->Num("bank", 0); c.playSeconds = (int64_t)s->Num("playSeconds", 0); c.deaths = I(s->Find("deaths"), 0);
    }
    if (auto *a = v.Find("weapons"))
        for (auto &x : a->arr)
            if (x.arr.size() == 2) c.weapons.push_back({(uint32_t)x.arr[0].num, (int)x.arr[1].num});
    if (auto *p = v.Find("position")) c.pos = {F(p->Find("x"), 0), F(p->Find("y"), 0), F(p->Find("z"), 0), F(p->Find("heading"), 0)};
    if (auto *a = v.Find("inventory"))
        for (auto &x : a->arr)
            if (x.type == json::Value::String) c.inventory.push_back(x.str);
    if (auto *m = v.Find("mods"); m && m->type == json::Value::Object)
        for (auto &kv : m->obj)
            if (kv.second.type == json::Value::String) c.modData[kv.first] = kv.second.str;
    c.revision = (uint32_t)v.Num("revision", 0);
    c.updatedAt = v.Str("updatedAt", "");
    std::vector<std::string> problems;
    if (!Validate(c, problems)) {
        err = "invalid profile:";
        for (auto &p : problems) err += " " + p + ";";
        return false;
    }
    return true;
}

}  // namespace character
