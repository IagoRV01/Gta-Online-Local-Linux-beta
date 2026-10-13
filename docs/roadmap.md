# Roadmap, risks and milestone plan

## Status

| Phase | Status | Evidence |
|---|---|---|
| 0 Audit (Windows + BC-250) | **done** | `docs/windows-server.md`, `docs/bc250-linux-client.md`, `docs/audit/*.txt` |
| 0 Reuse study (RAGECOOP etc.) | **done** | `docs/decisions.md` ADR-001/002 |
| 0 Protocol v1 → v4 | **done** (spec + code + tests) | `shared/protocol` + `shared/common`, all host tests passing |
| 0 Repo structure + build | **done** | `./build.sh` → tests, `LocalOnline.asi`, `LocalOnlineServer.exe`, `netprobe.exe` |
| 0 LAN path BC-250 → Windows UDP | **proven** | netprobe: RTT 1 ms, 0 % loss, 2 clients relayed |
| 1 YOU character on the BC-250 | **done** | the user created "Gori" in game (2026-10-07 23:08); death/respawn, exit to story and restart verified (see below) |
| 1+ Start screen (STORY MODE / LOCAL ONLINE) | **done** | replaces the official-Online path; verified 2026-10-07 23:32 |
| 2 Server persistence | **done** | SQLite + token accounts; cache deleted → server restored outfit + position; offline play + auto re-sync (2026-10-08 00:35–00:37) |
| 3 Fourth slot on the character wheel | not started | entry works through the start screen + F10; the wheel itself is cosmetic and fragile |
| 4 Minimal multiplayer (position/rotation/movement) | **done with netprobe as player 2** | puppet with the sender's look, 0.7–1.1 m error at 5 m/s (2026-10-08 00:58); a second real GTA client is pending |
| 5 Vehicles (enter/exit, position, rotation, velocity, ownership) | **done with netprobe** | probe drives a Sultan at 15 m/s: puppet car error 0.04–0.36 m (2026-10-08 01:17); Gori's own car (ydotool W) received by a Windows probe with the right driver/seat/speed (01:20) |
| 6 Combat (weapons, shooting, damage, death, respawn) | **done, verified in game** | protocol v3; probe ⇄ Gori damage both ways, kill feed, respawn; Gori's pistol hit to the chest = 26 PvP damage on the probe (2026-10-09 01:07) |
| 7 World sync (clock, weather, blackout, wanted display) | **done, verified in game** (2026-10-09 00:52–00:56: server 22:00 + RAIN applied in 5 s, story clock 02:00/OVERCAST restored on exit) | netprobe: admin time/weather/blackout pushed live, invalid weather rejected (2026-10-09 00:07); in-game check waits for the BC-250 memory fix |
| 8 Full persistence: economy + personal vehicles | **done, verified in game** (2026-10-09 00:53–00:56: +500 in the ledger; Sultan registered, stored, called out to the road, stored automatically on exit) | netprobe economy (cheat ignored, limits, ledger) and garage lifecycle incl. insurance fee and a refused transition (2026-10-09 00:34/00:43); SQLite storage tests; in-game check waits for the BC-250 memory fix |
| 8+ Online polish: Maze Bank (wallet/bank, ATMs), Online HUD (money, player list, gamer tags), post-death HUD fix | **built + server verified** (netprobe 2026-10-09 01:45: job → bank, shop → wallet, deposit, withdraw, overdraft refused; schema v1→v2 migration on the live DB) | in-game check pending a GTA restart |
| 8+ Online polish: properties/interiors/businesses/heists per character, internet purchases from the bank | **built + deployed** (2026-10-09 01:53) | `mods` in the character profile (tests), Gori Online switches profile with YOU; in-game check pending a GTA restart |
| 8+ Co-op missions (heists, business sales with other players) | not started | needs server-coordinated mission state + entity ownership (`docs/entity-ownership.md`) |
| 9–14 (GodEye, 3D map, cameras, web game) | not started | allowed to start now that the server path is stable; next in the plan |

### Story-mode protection: what was found and fixed (2026-10-07/08)

| Finding (in game) | Fix | Verified |
|---|---|---|
| The first death as YOU in a game session triggers GTA's restart, which rebuilds the player as the story character (and may bill the hospital to it) | The plugin respawns YOU itself at the nearest hospital; if the restart still happens, it is undone: YOU is rebuilt, the restart's story ped is deleted, the original hidden story ped is kept, and story cash is refunded | yes, 23:53 and 00:22 |
| After a YOU death, a later STORY death came back as a freemode ped (with YOU's clothes) | Story guard: a rolling snapshot (model, outfit, weapons) of the story character; a freemode respawn right after a story death is reverted | yes, 00:22 (Michael restored with 50 weapons) |
| Gori Online charged the story character's cash while YOU played | LocalOnline exports `LocalOnline_IsYouActive/GetMoney/AddMoney`; Gori Online uses YOU's money while YOU is active | built + deployed; purchase not yet exercised in game |
| Story saves | never written by the plugin; copied before every first switch per session | `SGTA5*` byte-identical to the pre-test backup after all tests |

## First milestone: how we get there

> BC-250 runs GTA V Story Mode through Proton, with a freemode character independent of Michael, Franklin and Trevor, whose appearance and position save and load correctly.

**Acceptance test.** Each step is checked against `LocalOnline.log` plus a screenshot.

1. Start GTA as Michael, Franklin or Trevor. Note the character, position and outfit.
2. F10 → *Play as YOU* → the creator opens on first use → create the character (name, heritage, hair, clothes) → CREATE.
3. Walk, run, sprint, drive a car (`veh` test command or any car), and shoot (pistol).
4. Change clothes (F10 → Edit appearance), then move somewhere else.
5. F10 → *Return to Story Mode*. The original character must be back **at the same spot, same outfit, same weapons**.
6. Quit GTA. Restart it and load story mode. The story save must be unchanged: same character and progress.
7. F10 → *Play as YOU*. The look and position from step 4 come back, loaded from `character.json`.
8. Delete `character.json` and keep the `.bak`. Corruption recovery must load the `.bak`.

**Then Phase 2:**
- Add SQLite storage to the server (`schema.sql`) with the `Auth` token and `ProfileRequest`/`ProfileSave` (revision checks).
- The client loads from the server on *Play as YOU* and uses the cache only when the server is down. The cache is marked read-only and is never uploaded over a newer server revision.

**Then Phase 4:**
- The in-GTA ENet client (the same `protocol.h`) sends `PlayerState` at 20 Hz, and remote players are spawned as puppet peds.
- First test: BC-250 GTA + netprobe walking in a circle at LSIA.
- Second test: BC-250 + the Windows GTA Legacy install with ScriptHookV, once Defender is handled.

## Risks

| # | Risk | Impact | Mitigation |
|---|---|---|---|
| R1 | **Story save integrity** when switching characters | high | CHANGE_PLAYER_PED keeps the story ped intact; per-session save backups; the wheel is disabled in YOU; never save the story while in YOU until tested |
| R2 | Game scripts reacting to a non-story player ped (respawn, mission triggers, the switch system) | medium | Mission and cutscene checks on entry; ped-change watchdog (if the game puts a story char back, we leave YOU cleanly); respawn tested in the milestone |
| R3 | SHVDN/.NET unusable under Proton (blocks RAGECOOP and C# mods) | medium | Native C++ only (ADR-001). Revisit in a prefix copy. |
| R12 | **BC-250 memory split**: 8 GB VRAM carve-out (set in BIOS on 2026-10-08) leaves 7.5 GiB RAM; GTA stalls and systemd-oomd killed Steam | high | BIOS UMA frame buffer back to 512 MB (user action; documented in troubleshooting.md) |
| R4 | **BC-250 thermals**: Tctl 98–102 °C while playing | high (hardware) | Check cooling and fan curve before long sessions; CPU budget for the plugin: no per-frame allocations, network on its own thread in Phase 4 |
| R5 | Memory headroom on the BC-250: 3 GiB available while playing, swap in use | medium | Keep the plugin small (1.3 MB); limit puppet entities; watch for stutter |
| R6 | DHCP moves the server IP (192.168.1.10) | medium | DHCP reservation on the router; `client.toml` holds the address (never hardcoded) |
| R7 | Mod interplay (GoriOnline, Menyoo, GTAResort run in the same game) | medium | Our own key (F10); nothing global is touched; the F6 HUD of GTAResort overlaps our menu (cosmetic) |
| R8 | Winsock/ENet inside GTA under Wine | low | GTAResort already uses UDP in-game; ENet uses plain Winsock |
| R9 | GTA updates break ScriptHookV (new build) | medium | Steam auto-update off for GTA if possible; pin build 3889; the plugin logs the game build |
| R10 | Windows Defender deletes ScriptHookV on the Windows test client | low (Phase 4) | Exclusion set by the user; it is their security decision |
| R11 | Two GTA clients with different mods or DLC lists produce different worlds | medium (Phase 7) | Server-owned clock and weather; ownership rules; same mod set on every client |
