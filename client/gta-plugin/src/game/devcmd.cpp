#include "game/devcmd.h"
#include <deque>
#include <fstream>
#include <sstream>
#include "common/log.h"
#include "engine/gta.h"

namespace devcmd {

static std::string g_path;
static bool g_on = false;
static std::deque<std::string> g_q;
static int g_last = 0;

void Init(const std::string &path, bool enabled) {
    g_path = path;
    g_on = enabled;
    if (g_on) std::ofstream(g_path, std::ios::trunc);   // drop stale commands from an earlier session
}

static void Read() {
    std::ifstream f(g_path);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    f.close();
    std::string text = ss.str();
    if (text.empty()) return;
    std::ofstream(g_path, std::ios::trunc);
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) { g_q.push_back(line); LOGI("devcmd", "-", "> %s", line.c_str()); }
    }
}

bool Next(std::string &line) {
    if (!g_on) return false;
    int now = gta::GameTimeMs();
    if (g_q.empty() && now - g_last >= 500) { g_last = now; Read(); }
    if (g_q.empty()) return false;
    line = g_q.front();
    g_q.pop_front();
    return true;
}

bool NextFor(const char *verbs, std::string &line) {
    if (!g_on) return false;
    int now = gta::GameTimeMs();
    if (now - g_last >= 500) { g_last = now; Read(); }
    std::string list = std::string(" ") + verbs + " ";
    for (auto it = g_q.begin(); it != g_q.end(); ++it) {
        std::string verb = it->substr(0, it->find(' '));
        if (list.find(" " + verb + " ") == std::string::npos) continue;
        line = *it;
        g_q.erase(it);
        return true;
    }
    return false;
}

void PushFront(const std::string &line) { g_q.push_front(line); }

}  // namespace devcmd
