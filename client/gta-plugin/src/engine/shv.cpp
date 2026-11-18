#include "engine/shv.h"

namespace shv {

void (*scriptRegister)(HMODULE, void (*)());
void (*scriptUnregister)(HMODULE);
void (*scriptWait)(DWORD);
void (*nativeInit)(uint64_t);
void (*nativePush64)(uint64_t);
uint64_t *(*nativeCall)();
void (*keyboardRegister)(KeyboardHandler);
void (*keyboardUnregister)(KeyboardHandler);
int (*worldGetAllPeds)(int *, int);
int (*worldGetAllVehicles)(int *, int);

bool Bind() {
    HMODULE h = GetModuleHandleA("ScriptHookV.dll");
    if (!h) h = LoadLibraryA("ScriptHookV.dll");
    if (!h) return false;
#define BIND(var, name) var = reinterpret_cast<decltype(var)>(GetProcAddress(h, name)); if (!var) return false;
    BIND(scriptRegister, "?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z");
    BIND(scriptUnregister, "?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z");
    BIND(scriptWait, "?scriptWait@@YAXK@Z");
    BIND(nativeInit, "?nativeInit@@YAX_K@Z");
    BIND(nativePush64, "?nativePush64@@YAX_K@Z");
    BIND(nativeCall, "?nativeCall@@YAPEA_KXZ");
    BIND(keyboardRegister, "?keyboardHandlerRegister@@YAXP6AXKGEHHHH@Z@Z");
    BIND(keyboardUnregister, "?keyboardHandlerUnregister@@YAXP6AXKGEHHHH@Z@Z");
#undef BIND
    worldGetAllPeds = reinterpret_cast<decltype(worldGetAllPeds)>(GetProcAddress(h, "?worldGetAllPeds@@YAHPEAHH@Z"));
    worldGetAllVehicles = reinterpret_cast<decltype(worldGetAllVehicles)>(GetProcAddress(h, "?worldGetAllVehicles@@YAHPEAHH@Z"));
    return true;
}

}  // namespace shv
