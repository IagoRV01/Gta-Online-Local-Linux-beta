# Decisions (ADR log)

Each entry: context → decision → evidence → consequences. Newest last.

## ADR-001 — Do not build on RAGECOOP-V (for now)

**Context.** RAGECOOP-V is the obvious candidate. It is story-mode co-op with player/vehicle/NPC sync, bullets, projectiles, ragdolls and scripting. The brief asks to check it before writing networking from scratch, and to check specifically "RAGECOOP Server → Windows" and "RAGECOOP Client → Proton/Linux".

**Findings (checked 2026-10-07):**

| Check | Result | Source |
|---|---|---|
| Upstream activity | Last release **1.5.4.7 (2023-11-20)**. Last commit 2024-04 (server Docker conversion). Website and Discord down (issue #87). | GitHub API `RAGECOOP/RAGECOOP-V` |
| Forks | One active fork, `Draythianna/RAGECOOP-V` (commits 2026-10-06, CI/packaging). `SammyVimes` bumped SHVDN to 3.7.0-nightly.118 (2026-04). Neither changes the client runtime. | GitHub API forks |
| **Server → Windows** | **Yes**: release assets include `RageCoop.Server-win-x64.zip` (.NET 6) | release 1.5.4.7 |
| **Client runtime** | ScriptHookV + **ScriptHookVDotNet 3.6+** + **.NET Framework 4.8**; uses ClearScript (V8, native DLLs) and Lidgren | upstream README |
| **Client → Proton on the BC-250** | **Not working today.** SHVDN crashed GTA at story load on Wine Mono, so it is disabled in the game folder. The .NET 4.8 installer fails in this Proton 11 prefix (exit 67 / 0x80070643). Community reports of SHVDN on Proton rely on `protontricks dotnet48` with older Proton versions. | this machine, plus the earlier project's notes; ProtonDB/Lemmy reports |
| Game build | RAGECOOP was last tested on 2023 builds; the BC-250 runs 1.0.3889 (2026). | |

**Decision.**
- Build our own client as a **native C++ ScriptHookV .asi**, which is proven to run on this BC-250.
- Use **ENet** (the transport RAGECOOP's Lidgren is also a variant of) and a small versioned protocol.
- **Use RAGECOOP as a reference design, not a dependency.** It is MIT-licensed, so its sync strategies (what to send, ped/vehicle interpolation tricks, ragdoll and bullet sync) can be studied with attribution.

**Revisit if:**
- SHVDN starts working in this prefix. Try it in a **copy** of the prefix: Proton 9/10 + `protontricks 271590 dotnet48`, never in the live prefix.
- Or a maintained fork switches to a native client.

## ADR-002 — Transport: ENet over UDP

**Options weighed:**
- raw UDP + our own reliability
- **ENet**
- QUIC (msquic/quiche)
- yojimbo / GameNetworkingSockets
- WebSocket (TCP)

**Decision.** ENet 1.3.18, vendored as C source.

**Why.**
- **Channels.** It provides unreliable-sequenced (state) and reliable-ordered (control/events) on one socket, which is exactly the split we need.
- **Built in:** fragmentation, RTT/loss statistics, timeouts, a peer limit.
- **Small and portable.** It is about 4k lines of C, MIT-licensed, and has no dependencies. It compiles with MinGW for the Windows exe and the in-GTA .asi, and with gcc natively on Linux (netprobe).
- **Fits Wine.** It uses plain Winsock, which Wine supports well.
- **QUIC** brings TLS and congestion control we do not need on a LAN. msquic under Wine is an unknown.
- **WebSocket** is TCP, so head-of-line blocking hurts real-time replication. It is kept for GodEye ↔ browser only.

**Evidence.** BC-250 (native Linux) → Windows server: RTT 1 ms, 0 % loss, 20 Hz snapshots, two clients seeing each other (2026-10-07).

## ADR-003 — C++ everywhere for the game path; TypeScript for GodEye

See [architecture.md](architecture.md#why-one-language-c-for-the-client-server-and-protocol). The client cannot be managed code (ADR-001). Sharing `protocol.h` and `character.cpp` between client and server removes a whole class of bugs.

## ADR-004 — Preserve the story characters with CHANGE_PLAYER_PED, not SET_PLAYER_MODEL

**Context.** Turning Michael into a freemode model with `SET_PLAYER_MODEL` destroys his ped, which takes his outfit, weapons and damage state with it. Restoring them by hand is fragile.

**Decision.**
- Create a **new** freemode ped and move the player into it with `CHANGE_PLAYER_PED`, which is the mechanism the game's own character switch uses.
- Keep the story ped as a hidden, frozen, invincible mission entity where it was.
- On exit, `CHANGE_PLAYER_PED` back to it and delete the freemode ped.
- Fallback if a script deleted the story ped meanwhile: `SET_PLAYER_MODEL` plus the outfit captured on entry.
- The story save files are copied once per game session before the first switch.

**Consequences.**
- The character wheel is disabled while YOU is active (Phase 3 will put YOU on the wheel).
- Entering requires being on foot, with no mission and no cutscene.

## ADR-006 — The plugin owns death/respawn while YOU is active (2026-10-07)

**Context.** GTA's death restart is code-driven (respawn_controller only draws WASTED and releases
`PAUSE_DEATH_ARREST_RESTART`, see the decompiled script). In game we observed that the first death
as YOU rebuilds the player as the story character, and that after it a story death can come back as
a freemode ped. The exact engine rule was not found.

**Decision.** Do not fight the engine blindly; make the outcome correct and verifiable:
resurrect YOU ourselves ~3 s (real time) after death at the nearest hospital; detect and undo a
story restart that still happens (rebuild YOU, delete the rebuilt story ped, keep the original
hidden one, refund story cash); in story mode keep a rolling snapshot of the story character and
revert a freemode respawn. Every step logs a WARN with the ped handles involved.

## ADR-007 — Own start screen instead of Rockstar's landing page

The landing page runs before ScriptHookV scripts exist (it can't be scripted), its GTA Online entry
goes to the official service, and on this install it is disabled in the game's settings anyway. The
plugin shows its own STORY MODE / LOCAL ONLINE screen once the game has loaded (config
`startup_menu`). There is no official-Online option in it.

## ADR-005 — Phase 1 persistence = local JSON cache with the server's schema

- The profile is stored at `<GTA>\LocalOnline\cache\character.json`.
- It uses exactly the JSON the server will store (`character::ToJson`), with `revision` for optimistic concurrency.
- Phase 2 makes the server authoritative. The file becomes a cache, refreshed from the server on every login.
