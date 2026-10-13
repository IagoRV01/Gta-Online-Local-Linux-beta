// Typed wrappers over the GTA natives the plugin uses. Gameplay code calls these instead of raw
// natives. Everything here must run on the script fiber (inside ScriptMain).
#pragma once
#include <cstdint>
#include <string>

namespace gta {

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Color { uint8_t r, g, b, a; };
constexpr Color kWhite{255, 255, 255, 255}, kBlack{0, 0, 0, 200}, kGrey{170, 170, 170, 255}, kAccent{93, 182, 229, 255},
                kYellow{240, 200, 80, 255}, kRed{224, 50, 50, 255}, kGreen{114, 204, 114, 255};

void Wait(unsigned ms);
// Called from every Wait(): keeps the network serviced while blocking UI (creator, menus,
// teleports) runs. Must not block. Reentrant calls are skipped.
void SetWaitHook(void (*hook)());
uint32_t Joaat(const char *s);
int GameTimeMs();

int PlayerId();
int PlayerPed();
bool PlayerPlaying();

bool Exists(int e);
bool Dead(int e);
Vec3 Pos(int e);
float Heading(int e);
void SetHeading(int e, float deg);
Vec3 Velocity(int e);
int Health(int e);
void Freeze(int e, bool on);
void SetVisible(int e, bool on);
void SetCollision(int e, bool on);
void SetInvincible(int e, bool on);
void MakeMission(int e);
bool InVehicle(int ped);
uint32_t ModelOf(int e);

bool LoadModel(uint32_t model, int timeoutMs = 8000);
void ReleaseModel(uint32_t model);
// Moves the player's ped to p (on foot), streams collision in, places it on the ground.
void Teleport(const Vec3 &p, float heading);
float GroundZ(float x, float y, float zTop, float fallback);

void FadeOut(int ms);   // blocking
void FadeIn(int ms);

// ---- HUD (call every frame)
void Notify(const std::string &text);
void Help(const std::string &text);
void Rect(float x, float y, float w, float h, Color c);    // centre-based, 0..1 screen units
void Text(const std::string &t, float x, float y, float scale, Color c, int align = 1, int font = 0, float wrapEnd = 1.f);
float TextWidth(const std::string &t, float scale, int font);     // screen units
void Big(const std::string &title, const std::string &sub, int ms);   // centred "big message" (shard-like)

// ---- controls (pad + keyboard through GTA's control actions)
void DisableAllControls();
void DisableControl(int action);
bool JustPressed(int action);    // works for disabled controls too
bool Pressed(int action);
// Blocking onscreen keyboard. Returns false if the player cancelled.
bool Keyboard(const char *title, const std::string &initial, int maxLen, std::string &out);

// GTA control action ids used by the UI.
enum Ctl { kUp = 172, kDown = 173, kLeft = 174, kRight = 175, kAccept = 201, kCancel = 202, kTabL = 205, kTabR = 206,
           kCharWheel = 19, kSelMichael = 166, kSelFranklin = 167, kSelTrevor = 168, kSelMultiplayer = 169 };

// ---- script camera
int CamCreate(const Vec3 &pos, const Vec3 &lookAt, float fov);
void CamMove(int cam, const Vec3 &pos, const Vec3 &lookAt, float fov);
void CamDestroy(int &cam);   // also returns to the gameplay camera

}  // namespace gta
