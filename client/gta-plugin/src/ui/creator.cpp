#include "ui/creator.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/appearance.h"
#include "game/devcmd.h"
#include "ui/menu.h"

namespace creator {

using namespace character;

// Heritage faces as GTA Online's creator lists them (face index -> name).
static const int kMothers[] = {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 45};
static const char *kMotherNames[] = {"Hannah", "Audrey", "Jasmine", "Giselle", "Amelia", "Isabella", "Zoe", "Ava", "Camila", "Violet", "Sophia",
                                     "Evelyn", "Nicole", "Ashley", "Grace", "Brianna", "Natalie", "Olivia", "Elizabeth", "Charlotte", "Emma", "Misty"};
static const int kFathers[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 42, 43, 44};
static const char *kFatherNames[] = {"Benjamin", "Daniel", "Joshua", "Noah", "Andrew", "Juan", "Alex", "Isaac", "Evan", "Ethan", "Vincent", "Angel",
                                     "Diego", "Adrian", "Gabriel", "Michael", "Santiago", "Kevin", "Louis", "Samuel", "Anthony", "Claude", "Niko", "John"};
static const char *kFeatureNames[kNumFaceFeatures] = {
    "Nose width", "Nose peak height", "Nose peak length", "Nose bone height", "Nose peak lowering", "Nose bone twist",
    "Eyebrow height", "Eyebrow depth", "Cheekbone height", "Cheekbone width", "Cheek width", "Eye opening", "Lip thickness",
    "Jaw width", "Jaw shape", "Chin height", "Chin depth", "Chin width", "Chin hole", "Neck thickness"};

template <size_t N> static int IndexOf(const int (&arr)[N], int v) {
    for (size_t i = 0; i < N; ++i) if (arr[i] == v) return (int)i;
    return 0;
}
static int Wrap(int v, int n) { return n <= 0 ? 0 : ((v % n) + n) % n; }
static std::string Pct(float f) { char b[16]; std::snprintf(b, sizeof b, "%d%%", (int)std::lround(f * 100)); return b; }
static std::string Int(int v) { return std::to_string(v); }

namespace {
struct Session {
    Character w;
    int ped = 0;
    int cam = 0;
    int camMode = 0;        // 0 face, 1 body
    int lastLook = 0;
    float heading = 0;      // ped heading while in the creator (rotate with Q/E or LB/RB)
    bool dirty = true, done = false, confirmed = false;
    std::function<int(bool)> swap;
    std::vector<std::unique_ptr<ui::Menu>> menus;
    std::vector<ui::Menu *> stack;

    ui::Menu *NewMenu(const char *sub) {
        menus.push_back(std::make_unique<ui::Menu>());
        auto *m = menus.back().get();
        m->title = "CREATE CHARACTER";
        m->subtitle = sub;
        return m;
    }
    void Open(ui::Menu *m, int cmode) { stack.push_back(m); camMode = cmode; }

    // ---- item builders
    void Slider(ui::Menu *m, const std::string &label, float *v, float lo, float hi, float step) {
        m->items.push_back({label, [v] { return Pct(*v); }, [this, v, lo, hi, step](int d) {
            *v = std::fmax(lo, std::fmin(hi, std::round((*v + d * step) / step) * step)); dirty = true; }, nullptr, ""});
    }
    void Overlay(ui::Menu *m, const std::string &label, int id, bool opacity, bool color) {
        OverlaySlot *o = &w.overlays[id];
        m->items.push_back({label, [o] { return o->index == kNoOverlay ? std::string("None") : Int(o->index + 1); },
            [this, o, id](int d) {
                int n = appearance::OverlayCount(id);
                int cur = o->index == kNoOverlay ? -1 : o->index;      // -1 = none, 0..n-1
                cur = Wrap(cur + 1 + d, n + 1) - 1;
                o->index = cur < 0 ? kNoOverlay : cur; dirty = true; }, nullptr, ""});
        if (opacity) Slider(m, label + " opacity", &o->opacity, 0.f, 1.f, 0.05f);
        if (color)
            m->items.push_back({label + " colour", [o] { return Int(o->color); },
                [this, o](int d) { o->color = Wrap(o->color + d, appearance::HairTintCount()); o->color2 = o->color; dirty = true; }, nullptr, ""});
    }
    void Clothing(ui::Menu *m, const std::string &label, int comp) {
        Variation *v = &w.components[comp];
        m->items.push_back({label, [this, v, comp] { return Int(v->drawable + 1) + "/" + Int(appearance::DrawableCount(ped, comp)); },
            [this, v, comp](int d) { v->drawable = Wrap(v->drawable + d, appearance::DrawableCount(ped, comp)); v->texture = 0; dirty = true; }, nullptr, ""});
        m->items.push_back({label + " style", [this, v, comp] { return Int(v->texture + 1) + "/" + Int(appearance::TextureCount(ped, comp, v->drawable)); },
            [this, v, comp](int d) { v->texture = Wrap(v->texture + d, appearance::TextureCount(ped, comp, v->drawable)); dirty = true; }, nullptr, ""});
    }
    void Prop(ui::Menu *m, const std::string &label, int anchor) {
        PropSlot *p = &w.props[anchor];
        m->items.push_back({label, [p] { return p->drawable < 0 ? std::string("None") : Int(p->drawable + 1); },
            [this, p, anchor](int d) { p->drawable = Wrap(p->drawable + 1 + d, appearance::PropCount(ped, anchor) + 1) - 1; p->texture = 0; dirty = true; }, nullptr, ""});
        m->items.push_back({label + " style", [p] { return p->drawable < 0 ? std::string("-") : Int(p->texture + 1); },
            [this, p, anchor](int d) { if (p->drawable >= 0) { p->texture = Wrap(p->texture + d, appearance::PropTextureCount(ped, anchor, p->drawable)); dirty = true; } }, nullptr, ""});
    }

    void EditName() {
        std::string out;
        if (gta::Keyboard("Name", w.name, 24, out)) {
            if (ValidName(out)) w.name = out;
            else gta::Notify("~r~Invalid name.~s~ 2-24 letters, digits, space, _ - .");
        }
    }

    void Build() {
        ui::Menu *main = NewMenu("Main");
        // Heritage
        ui::Menu *her = NewMenu("Heritage");
        her->items.push_back({"Mother", [this] { return std::string(kMotherNames[IndexOf(kMothers, w.blend.shapeMother)]); },
            [this](int d) { int i = Wrap(IndexOf(kMothers, w.blend.shapeMother) + d, (int)std::size(kMothers)); w.blend.shapeMother = w.blend.skinMother = kMothers[i]; dirty = true; }, nullptr, ""});
        her->items.push_back({"Father", [this] { return std::string(kFatherNames[IndexOf(kFathers, w.blend.shapeFather)]); },
            [this](int d) { int i = Wrap(IndexOf(kFathers, w.blend.shapeFather) + d, (int)std::size(kFathers)); w.blend.shapeFather = w.blend.skinFather = kFathers[i]; dirty = true; }, nullptr, ""});
        Slider(her, "Resemblance (father)", &w.blend.shapeMix, 0.f, 1.f, 0.05f);
        Slider(her, "Skin tone (father)", &w.blend.skinMix, 0.f, 1.f, 0.05f);
        // Features
        ui::Menu *feat = NewMenu("Face features");
        for (int i = 0; i < kNumFaceFeatures; ++i) {
            float *f = &w.face[i];
            feat->items.push_back({kFeatureNames[i], [f] { char b[16]; std::snprintf(b, sizeof b, "%+.1f", *f); return std::string(b); },
                [this, f](int d) { *f = std::fmax(-1.f, std::fmin(1.f, std::round((*f + d * 0.1f) * 10) / 10)); dirty = true; }, nullptr, ""});
        }
        // Appearance
        ui::Menu *app = NewMenu("Appearance");
        app->items.push_back({"Hair", [this] { return Int(w.components[Hair].drawable + 1); },
            [this](int d) { auto &v = w.components[Hair]; v.drawable = Wrap(v.drawable + d, appearance::DrawableCount(ped, Hair)); v.texture = 0; dirty = true; }, nullptr, ""});
        app->items.push_back({"Hair colour", [this] { return Int(w.hairColor); }, [this](int d) { w.hairColor = Wrap(w.hairColor + d, appearance::HairTintCount()); dirty = true; }, nullptr, ""});
        app->items.push_back({"Highlights", [this] { return Int(w.hairHighlight); }, [this](int d) { w.hairHighlight = Wrap(w.hairHighlight + d, appearance::HairTintCount()); dirty = true; }, nullptr, ""});
        Overlay(app, "Eyebrows", Eyebrows, true, true);
        Overlay(app, "Beard", FacialHair, true, true);
        app->items.push_back({"Eye colour", [this] { return Int(w.eyeColor); }, [this](int d) { w.eyeColor = Wrap(w.eyeColor + d, 32); dirty = true; }, nullptr, ""});
        Overlay(app, "Ageing", Ageing, true, false);
        Overlay(app, "Complexion", Complexion, true, false);
        Overlay(app, "Blemishes", Blemishes, true, false);
        Overlay(app, "Freckles", Moles, true, false);
        Overlay(app, "Sun damage", SunDamage, true, false);
        Overlay(app, "Makeup", Makeup, true, false);
        Overlay(app, "Blush", Blush, true, true);
        Overlay(app, "Lipstick", Lipstick, true, true);
        // Clothing
        ui::Menu *clo = NewMenu("Clothing");
        Clothing(clo, "Top", Tops);
        Clothing(clo, "Undershirt", Undershirt);
        Clothing(clo, "Arms / gloves", Torso);
        Clothing(clo, "Legs", Legs);
        Clothing(clo, "Shoes", Feet);
        Clothing(clo, "Neck / chains", Accessory);
        Clothing(clo, "Mask", Mask);
        Prop(clo, "Hat", 0);
        Prop(clo, "Glasses", 1);
        Prop(clo, "Ears", 2);
        Prop(clo, "Watch", 6);
        Prop(clo, "Bracelet", 7);

        main->items.push_back({"Gender", [this] { return std::string(w.female ? "Female" : "Male"); }, [this](int) {
            bool f = !w.female;
            Character d = MakeDefault(f);
            w.female = f; w.components = d.components; w.props = d.props; w.overlays[FacialHair] = d.overlays[FacialHair];
            ped = swap(f);
            gta::Freeze(ped, true);
            gta::SetHeading(ped, heading);
            dirty = true; }, nullptr, "Changing gender resets clothes and hair."});
        main->items.push_back({"Heritage", nullptr, nullptr, [this, her] { Open(her, 0); }, "Parents, resemblance and skin tone."});
        main->items.push_back({"Features", nullptr, nullptr, [this, feat] { Open(feat, 0); }, "Nose, brows, cheeks, jaw, chin..."});
        main->items.push_back({"Appearance", nullptr, nullptr, [this, app] { Open(app, 0); }, "Hair, eyebrows, beard, eyes, makeup."});
        main->items.push_back({"Clothing", nullptr, nullptr, [this, clo] { Open(clo, 1); }, "Q / E (LB / RB) turns the character."});
        main->items.push_back({"Name", [this] { return w.name; }, nullptr, [this] { EditName(); }, ""});
        main->items.push_back({"CREATE / SAVE", nullptr, nullptr, [this] {
            if (!ValidName(w.name) || w.name == "Player") { gta::Notify("Choose a ~y~name~s~ first."); EditName(); if (!ValidName(w.name)) return; }
            confirmed = done = true; }, "Save this character and continue."});
        stack.push_back(main);
    }

    void UpdateCamera() {
        gta::Vec3 p = gta::Pos(ped);
        float h = heading * 3.14159265f / 180.f;
        float fx = -std::sin(h), fy = std::cos(h);   // ped's forward vector
        gta::Vec3 at{p.x, p.y, p.z + (camMode == 0 ? 0.62f : 0.0f)};
        float dist = camMode == 0 ? 0.95f : 2.7f;
        gta::Vec3 eye{p.x + fx * dist, p.y + fy * dist, at.z + (camMode == 0 ? 0.03f : 0.25f)};
        if (!cam) cam = gta::CamCreate(eye, at, camMode == 0 ? 38.f : 45.f);
        else gta::CamMove(cam, eye, at, camMode == 0 ? 38.f : 45.f);
        int now = gta::GameTimeMs();
        if (now - lastLook > 1500) {   // keep the head facing the camera instead of idling around
            lastLook = now;
            N(N_TASK_LOOK_AT_COORD, ped, eye.x, eye.y, eye.z, 2000, 0, 2);
        }
    }

    // Remote test commands understood while the creator is open.
    void Commands() {
        std::string line;
        while (devcmd::NextFor("ui back name cancel", line)) {
            std::istringstream in(line);
            std::string verb;
            in >> verb;
            if (verb == "ui") {                    // ui <label prefix> [dir]   (dir 0 = select)
                std::string rest;
                std::getline(in, rest);
                int dir = 0;
                size_t sp = rest.find_last_of(' ');
                if (sp != std::string::npos && (rest.substr(sp + 1) == "1" || rest.substr(sp + 1) == "-1")) { dir = std::stoi(rest.substr(sp + 1)); rest = rest.substr(0, sp); }
                while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
                if (!stack.back()->Activate(rest, dir)) LOGW("creator", "-", "ui: no item '%s' in %s", rest.c_str(), stack.back()->subtitle.c_str());
            } else if (verb == "back") {
                if (stack.size() > 1) { stack.pop_back(); camMode = 0; }
            } else if (verb == "name") {
                std::string n;
                std::getline(in, n);
                while (!n.empty() && n[0] == ' ') n.erase(0, 1);
                if (ValidName(n)) w.name = n; else LOGW("creator", "-", "name rejected: '%s'", n.c_str());
            } else if (verb == "cancel") {
                done = true;
            }
        }
    }
};
}  // namespace

bool Run(Character &c, bool firstTime, const std::function<int(bool)> &swapGender) {
    Session s;
    s.w = c;
    s.swap = swapGender;
    s.ped = gta::PlayerPed();
    s.heading = gta::Heading(s.ped);
    N(N_CLEAR_PED_TASKS_IMMEDIATELY, s.ped);
    N(N_SET_CURRENT_PED_WEAPON, s.ped, gta::Joaat("WEAPON_UNARMED"), 1);
    gta::Freeze(s.ped, true);
    gta::SetInvincible(s.ped, true);
    s.Build();
    LOGI("creator", c.name.c_str(), "opened (%s)", firstTime ? "new character" : "edit");

    while (!s.done) {
        s.Commands();
        if (s.done) break;
        if (!s.stack.back()->Frame()) {
            if (s.stack.size() > 1) { s.stack.pop_back(); s.camMode = 0; }
            else if (!firstTime) s.done = true;              // edit: back = discard changes
            else gta::Notify("Finish with ~y~CREATE / SAVE~s~.");
        }
        if (gta::Pressed(gta::kTabL)) s.heading += 2.5f;
        if (gta::Pressed(gta::kTabR)) s.heading -= 2.5f;
        gta::SetHeading(s.ped, s.heading);
        if (s.dirty) { appearance::ApplyLook(s.ped, s.w); s.dirty = false; }
        s.UpdateCamera();
        gta::Help("~INPUT_FRONTEND_LB~ ~INPUT_FRONTEND_RB~ turn   ~INPUT_FRONTEND_ACCEPT~ select   ~INPUT_FRONTEND_CANCEL~ back");
        gta::Wait(0);
    }
    gta::CamDestroy(s.cam);
    s.ped = gta::PlayerPed();
    gta::Freeze(s.ped, false);
    gta::SetInvincible(s.ped, false);
    if (s.confirmed) {
        c = s.w;
        LOGI("creator", c.name.c_str(), "confirmed (%s, mother %d father %d)", c.female ? "female" : "male", c.blend.shapeMother, c.blend.shapeFather);
    } else {
        // Back out of an edit: put the previous look back (the gender may have been swapped meanwhile).
        if (s.w.female != c.female) s.ped = swapGender(c.female);
        appearance::ApplyLook(s.ped, c);
        LOGI("creator", c.name.c_str(), "closed without saving");
    }
    return s.confirmed;
}

}  // namespace creator
