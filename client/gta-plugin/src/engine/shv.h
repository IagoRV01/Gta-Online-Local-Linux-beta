// ScriptHookV binding and native invocation. The only file that knows how natives are called.
// Exports are resolved with GetProcAddress (MSVC-mangled names) so no ScriptHookV SDK/.lib is needed
// and the .asi builds with MinGW (w64devkit).
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include "engine/natives.h"

namespace shv {

typedef void (*KeyboardHandler)(DWORD key, WORD repeats, BYTE scanCode, BOOL isExtended, BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow);

extern void (*scriptRegister)(HMODULE, void (*)());
extern void (*scriptUnregister)(HMODULE);
extern void (*scriptWait)(DWORD);
extern void (*nativeInit)(uint64_t);
extern void (*nativePush64)(uint64_t);
extern uint64_t *(*nativeCall)();
extern void (*keyboardRegister)(KeyboardHandler);
extern void (*keyboardUnregister)(KeyboardHandler);

bool Bind();   // false if ScriptHookV.dll is missing or an export was not found

}  // namespace shv

// GTA's scrVector: three floats, each padded to 8 bytes. Only used at the native boundary.
struct NVec3 { float x; uint32_t _a; float y; uint32_t _b; float z; uint32_t _c; };

template <typename T> inline void NPush(T v) {
    static_assert(sizeof(T) <= 8, "native argument too big");
    uint64_t u = 0; std::memcpy(&u, &v, sizeof(T)); shv::nativePush64(u);
}
// N<Ret>(N_<NATIVE>, args...) — calls a native by hash (hashes generated into engine/natives.h).
template <typename R = void, typename... A> inline R N(uint64_t hash, A... args) {
    shv::nativeInit(hash);
    (NPush(args), ...);
    uint64_t *r = shv::nativeCall();
    if constexpr (!std::is_void_v<R>) { R out; std::memcpy(&out, r, sizeof(R)); return out; }
}
inline bool NB(int v) { return v != 0; }
