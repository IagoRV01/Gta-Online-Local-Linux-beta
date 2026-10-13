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
    return true;
}

}  // namespace shv
