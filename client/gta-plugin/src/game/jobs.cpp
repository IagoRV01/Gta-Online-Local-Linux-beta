#include "game/jobs.h"
#include <cstdio>
#include "common/log.h"
#include "game/net_client.h"
#include "game/remote.h"
#include "game/session.h"

namespace jobs {

namespace {
proto::JobListMsg g_list;
proto::JobStateMsg g_state;   // jobId 0 = none
proto::RaceStandingsMsg g_standings;
bool g_hosting = false;
int g_rev = 0;

std::string NameOf(uint16_t id) { return id && id == net::SelfNetId() ? std::string("you") : remote::NameOf(id); }
std::string F(float v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return b; }
long long StartsInMs() {
    double now = remote::ServerNowMs();
    if (!g_state.startAt || now < 0) return 0;
    long long d = (long long)g_state.startAt - (long long)now;
    return d == 0 ? -1 : d;
}
}  // namespace

bool Open(const std::string &title, const proto::Vec3 &start, uint32_t payout, uint8_t kind) {
    if (!net::Connected() || !session::InYou()) return false;
    proto::JobOpenMsg m;
    m.title = title.substr(0, proto::kMaxJobTitle);
    m.start = start;
    m.payout = payout;
    m.kind = kind;
    wire::Writer w;
    proto::Put(w, m);
    net::SendEvent(w);
    g_hosting = true;
    LOGI("jobs", "-", "opening %s \"%s\" (payout %u)", kind == proto::kJobRace ? "race" : kind == proto::kJobDeathmatch ? "deathmatch" : kind == proto::kJobSurvival ? "survival" : "job", m.title.c_str(), payout);
    return true;
}

void Update(uint8_t status, uint16_t step, const proto::Vec3 &objective, const std::string &text) {
    if (!net::Connected() || !g_hosting) return;
    proto::JobUpdateMsg m;
    m.status = status; m.step = step; m.objective = objective; m.text = text.substr(0, proto::kMaxJobText);
    wire::Writer w;
    proto::Put(w, m);
    net::SendEvent(w);
    if (status >= proto::kJobPassed) g_hosting = false;
}

void Join(uint32_t jobId) {
    if (!net::Connected()) return;
    wire::Writer w;
    proto::Put(w, proto::JobJoinMsg{jobId});
    net::SendEvent(w);
    if (jobId) g_hosting = false;
    else { g_hosting = false; g_state = {}; ++g_rev; }
    LOGI("jobs", "-", jobId ? "joining job %u" : "leaving our job%.0u", jobId);
}

void OnList(const proto::JobListMsg &m) {
    g_list = m;
    ++g_rev;
}

void OnState(const proto::JobStateMsg &m) {
    bool same = g_state.jobId == m.jobId;
    if (m.status == proto::kJobClosed) {
        // closed after passing/failing: keep that end state readable; closed while running: the host gave up
        if (same && (g_state.status == proto::kJobPassed || g_state.status == proto::kJobFailed)) { ++g_rev; return; }
        LOGI("jobs", "-", "job %u closed", m.jobId);
    } else if (!same) { LOGI("jobs", "-", "in job %u (host %s)", m.jobId, NameOf(m.host).c_str()); g_standings = {}; }
    else if (m.status != g_state.status) LOGI("jobs", "-", "job %u status %u", m.jobId, m.status);
    g_state = m;
    ++g_rev;
}

void OnDisconnected() {
    g_list = {}; g_state = {}; g_standings = {}; g_hosting = false; ++g_rev;
}

void RaceProgress(uint16_t checkpoints, uint32_t finishMs) {
    if (!net::Connected() || !g_state.jobId || (g_state.kind != proto::kJobRace && g_state.kind != proto::kJobSurvival)) return;
    wire::Writer w;
    proto::Put(w, proto::RaceProgressMsg{checkpoints, finishMs});
    net::SendEvent(w);
}

void OnStandings(const proto::RaceStandingsMsg &m) {
    if (m.jobId != g_state.jobId) return;
    g_standings = m;
    ++g_rev;
}

std::string StandingsText() {
    std::string out;
    if (!g_state.jobId || g_standings.jobId != g_state.jobId) return out;
    for (const auto &e : g_standings.entries)
        out += NameOf(e.netId) + "\t" + std::to_string(e.checkpoints) + "\t" + std::to_string(e.finishMs) + "\t" + (e.netId == net::SelfNetId() ? "1" : "0") + "\n";
    return out;
}

std::string ListText() {
    std::string out;
    for (const auto &j : g_list.jobs)
        out += std::to_string(j.id) + "\t" + NameOf(j.host) + "\t" + j.title + "\t" + F(j.start.x) + "\t" + F(j.start.y) + "\t" + F(j.start.z) +
               "\t" + std::to_string(j.members) + "\t" + std::to_string(j.status) + "\t" + std::to_string(j.kind) + "\n";
    return out;
}

std::string StateText() {
    if (!g_state.jobId) return "";
    std::string names;
    for (uint16_t id : g_state.members) names += (names.empty() ? "" : ",") + NameOf(id);
    return std::to_string(g_state.jobId) + "\t" + NameOf(g_state.host) + "\t" + std::to_string(g_state.status) + "\t" + std::to_string(g_state.step) +
           "\t" + F(g_state.objective.x) + "\t" + F(g_state.objective.y) + "\t" + F(g_state.objective.z) + "\t" + std::to_string(g_state.payout) +
           "\t" + g_state.text + "\t" + names + "\t" + (g_hosting ? "1" : "0") + "\t" + std::to_string(g_state.kind) + "\t" + std::to_string(StartsInMs());
}

int Revision() { return g_rev; }

}  // namespace jobs
