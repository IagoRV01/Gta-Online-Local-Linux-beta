#include "common/log.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace logx {

static FILE *g_file = nullptr;
static Level g_min = Level::Info;
static bool g_echo = false;
static std::vector<std::string> g_recent;
static std::mutex g_mu;
static const size_t kRecentMax = 40;

void Open(const std::string &path, size_t maxBytes) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file) std::fclose(g_file);
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::fclose(f);
        if (size > 0 && (size_t)size > maxBytes) {
            std::string old = path + ".1";
            std::remove(old.c_str());
            std::rename(path.c_str(), old.c_str());
        }
    }
    g_file = std::fopen(path.c_str(), "a");
}

void SetMinLevel(Level lvl) { g_min = lvl; }
void SetEcho(bool on) { g_echo = on; }
const std::vector<std::string> &Recent() { return g_recent; }

void Write(Level lvl, const char *component, const char *entity, const char *fmt, ...) {
    if (lvl < g_min) return;
    static const char *names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    int ms = (int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char ts[32];
    std::snprintf(ts, sizeof ts, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    char body[1200];
    std::snprintf(body, sizeof body, "%s [%s] [%s] %s", names[(int)lvl], component, entity && *entity ? entity : "-", msg);

    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file) { std::fprintf(g_file, "%s %s\n", ts, body); std::fflush(g_file); }
    if (g_echo) std::printf("%s %s\n", ts, body);
    g_recent.emplace_back(body);
    if (g_recent.size() > kRecentMax) g_recent.erase(g_recent.begin());
}

}  // namespace logx
