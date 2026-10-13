// Records hardware exceptions raised inside LocalOnline.asi (code + module offsets) to the log.
#pragma once
#include <windows.h>

namespace crashlog {
void InstallCrashLogger(HMODULE self);
}
