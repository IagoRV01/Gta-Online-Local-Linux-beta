// Structured log shared by the server and the GTA client.
// Line format (one event per line, grep-friendly):
//   2026-10-07 22:41:03.123 INFO  [character] [player:Alex] profile loaded (v3, 1.2 KB)
//   timestamp               level [component] [player/entity or -] event
// Pure C++: no GTA, no Windows-only APIs, so it also runs in the host tests.
#pragma once
#include <string>
#include <vector>

namespace logx {

enum class Level { Debug, Info, Warn, Error };

// Opens (appends to) the log file; rotates an existing file bigger than maxBytes to <path>.1 first.
void Open(const std::string &path, size_t maxBytes = 8u << 20);
void SetMinLevel(Level lvl);          // DEBUG=true in the config -> Level::Debug
void SetEcho(bool toStdout);          // the server echoes to the console, the client does not
void Write(Level lvl, const char *component, const char *entity, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
const std::vector<std::string> &Recent();   // last lines (no timestamp), newest last, for in-game HUDs

}  // namespace logx

#define LOGD(comp, ent, ...) ::logx::Write(::logx::Level::Debug, comp, ent, __VA_ARGS__)
#define LOGI(comp, ent, ...) ::logx::Write(::logx::Level::Info, comp, ent, __VA_ARGS__)
#define LOGW(comp, ent, ...) ::logx::Write(::logx::Level::Warn, comp, ent, __VA_ARGS__)
#define LOGE(comp, ent, ...) ::logx::Write(::logx::Level::Error, comp, ent, __VA_ARGS__)
