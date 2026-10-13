// Co-op jobs (protocol v9). A host's mission script (Gori Online) opens a job and reports its progress;
// other players join it and follow the host's objective; the server pays the members when it passes.
// Gori Online drives this through the LocalOnline_Job* exports (main.cpp).
#pragma once
#include <cstdint>
#include <string>
#include "protocol/protocol.h"

namespace jobs {

bool Open(const std::string &title, const proto::Vec3 &start, uint32_t payout, uint8_t kind = proto::kJobMission);
void Update(uint8_t status, uint16_t step, const proto::Vec3 &objective, const std::string &text);
void Join(uint32_t jobId);                 // 0 = leave
void OnList(const proto::JobListMsg &m);
void OnState(const proto::JobStateMsg &m);
// races (v10): our progress in the race we are in, and everyone's standing
void RaceProgress(uint16_t checkpoints, uint32_t finishMs);
void OnStandings(const proto::RaceStandingsMsg &m);
// "name\tcheckpoints\tfinishMs\tme(0/1)\n" per racer, in race order; "" when not racing
std::string StandingsText();
void OnDisconnected();
// (host name "you" = our own job) "id\thost name\ttitle\tx\ty\tz\tmembers\tstatus\tkind\n" per open job
std::string ListText();
// "id\thost name\tstatus\tstep\tx\ty\tz\tpayout\ttext\tmember names (comma separated)\thosting(0/1)\tkind\tstartsInMs", "" when in no job
// (startsInMs: until the race start, negative once started, 0 when there is no start time)
std::string StateText();
int Revision();                            // changes whenever the list or our job's state changes

}  // namespace jobs
