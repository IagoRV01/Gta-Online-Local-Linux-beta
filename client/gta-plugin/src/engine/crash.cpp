// Vectored exception logger: when a hardware exception (access violation, illegal instruction,
// divide by zero...) is raised by code inside LocalOnline.asi, write the exception code, the module
// offset of the faulting instruction and a short stack of module offsets to the log. Resolve them
// with: tools/w64devkit/bin/addr2line -e build/LocalOnline.dbg.asi 0x<offset+imagebase>.
// It never handles the exception (returns CONTINUE_SEARCH), it only records it.
#include "engine/crash.h"
#include <cstdint>
#include "common/log.h"

namespace crashlog {

static uintptr_t g_base = 0, g_end = 0;

static bool Inside(uintptr_t a) { return a >= g_base && a < g_end; }

static LONG CALLBACK Handler(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // only real faults; C++ throws (0xE06D7363) and debugger/OS notifications are ignored
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t addr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    if (!Inside(addr)) return EXCEPTION_CONTINUE_SEARCH;
    static int reported = 0;
    if (++reported > 5) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t info = ep->ExceptionRecord->NumberParameters > 1 ? (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
    LOGE("crash", "-", "exception 0x%08lX at LocalOnline.asi+0x%llX (data address 0x%llX)", code, (unsigned long long)(addr - g_base), (unsigned long long)info);
    void *frames[24];
    USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        uintptr_t f = (uintptr_t)frames[i];
        if (Inside(f)) LOGE("crash", "-", "  #%u LocalOnline.asi+0x%llX", i, (unsigned long long)(f - g_base));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallCrashLogger(HMODULE self) {
    auto *dos = (IMAGE_DOS_HEADER *)self;
    auto *nt = (IMAGE_NT_HEADERS *)((uint8_t *)self + dos->e_lfanew);
    g_base = (uintptr_t)self;
    g_end = g_base + nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(0, Handler);
}

}  // namespace crashlog
