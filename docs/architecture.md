# Architecture

## Physical layout (fixed)

```
                 WINDOWS PC  (WINDOWS-PC, 192.168.1.10 wired)
                 ─────────────────────────────────────────
                 LocalOnlineServer.exe   UDP 4499 (ENet)      ← authority
                   ├─ session/auth, world state, ownership
                   ├─ storage (SQLite → PostgreSQL later)
                   └─ (Phase 9+) GodEye backend: HTTP/WS 8080
                 (Phase 12+) Camera Client = GTA Legacy + LocalOnline.asi in camera mode
                              │
                         LAN (1 Gbps wired)
                              │
          ┌───────────────────┴───────────────────┐
   BC-250 (Bazzite, 192.168.1.20)          future PC #3
   Steam + Proton Experimental              (Windows or Linux/Proton)
   GTA V Legacy 1.0.3889 + ScriptHookV      GTA V + LocalOnline.asi
   LocalOnline.asi  ← game client           ← game client
```

The server never needs GTA. The BC-250 is never the server, except for throwaway tests.

## Software components

| Component | Language / tech | Where | Status |
|---|---|---|---|
| `client/gta-plugin` (LocalOnline.asi) | C++20, ScriptHookV native calls, MinGW (w64devkit) | inside GTA on each client | **Phase 1 implemented** |
| `server/game-server` (LocalOnlineServer.exe) | C++20, ENet 1.3.18, SQLite 3.53.4 (Phase 2) | Windows | handshake + state relay working |
| `shared/protocol` | header-only C++ (wire format v1) | compiled into both sides | done, unit-tested |
| `shared/common` | C++: character model + validation, JSON, TOML, log | both sides | done, unit-tested |
| `tools/netprobe` | C++, ENet | Windows + Linux (native) | done: fake client for LAN/load tests |
| `godeye/*` | TypeScript (Node 24, no packages) + WebSocket; canvas 2D map, later Three.js | Windows | **Phase 9 done** (2D map) |
| `tools/godeye-map` | C# + CodeWalker | any PC with GTA V | makes GodEye's map data from the local game files |

### Why one language (C++) for the client, server and protocol

- The client **must** be native C++. SHVDN/.NET does not run in the BC-250's Proton prefix (see [decisions.md](decisions.md)).
- Writing the server in the same language lets both sides compile **the same** `protocol.h` and `character.cpp` (validation). There is no schema drift and no second implementation of the binary format to keep in sync.
- Same toolchain: one `build.sh` builds tests, client and server. Nothing new needs installing on Windows (no Visual Studio, no Rust).
- GodEye is a separate process (Node/TS). It talks to the game server over a local admin API, never to GTA. The protocol docs plus JSON schemas in `shared/schemas` are its contract.

## Process boundaries and trust

```
GTA client  ──(ENet/UDP, binary v1)──►  Game server  ◄──(local admin API)── GodEye backend ◄──(WS/HTTPS)── browser
 untrusted                               authority                          authN/authZ/rate-limit       untrusted
```

- Clients send **requests** and **their own ped's state**. The server validates everything with the same `character::Validate` the client uses, plus rate limits, size limits and sequence checks.
- Browsers never reach GTA or the game server directly.

## Client plugin internals (`client/gta-plugin/src`)

| Module | Role |
|---|---|
| `engine/shv.*` | binds ScriptHookV exports by name (no SDK/.lib) and calls natives by hash |
| `engine/natives.h` | generated from alloc8or's nativedb. An unknown native name is a build error. |
| `engine/gta.*` | typed native wrappers (entities, HUD, controls, camera, fades) |
| `engine/crash.*` | logs faults inside the .asi with module offsets (addr2line on `build/LocalOnline.dbg.asi`) |
| `game/session.*` | **STORY ⇄ YOU mode switch**, death/respawn tracking, autosave, story-save backup |
| `game/appearance.*` | applies a `Character` to a freemode ped (head blend, micro-morphs, overlays, clothes, props, tattoos) and reads state back |
| `game/profile_store.*` | local profile cache, written atomically (temp + replace, `.bak`) |
| `game/devcmd.*` | `LocalOnline.cmd` remote test channel (only with `remote_commands = true`) |
| `ui/menu.*`, `ui/creator.*` | F10 menu and CREATE CHARACTER, driven by GTA control actions (keyboard + pad) |

## Repository layout

```
client/gta-plugin   LocalOnline.asi sources + natives generator
client/linux        deploy.sh / rcmd.sh (run from the dev PC against the BC-250)
server/game-server  LocalOnlineServer.exe sources
server/windows      start-server.bat, setup-firewall.ps1
server/database     schema.sql (Phase 2)
shared/protocol     wire.h, protocol.h  (v1)
shared/common       character, json, toml, log
shared/schemas      JSON schema of the character profile (for GodEye/tools)
godeye/             GodEye backend (Node) + web app; data/map.json is generated locally
tools/godeye-map    GodEye map data generator (C#, CodeWalker)
tools/audit         Phase 0 audit scripts (Windows + BC-250)
tools/netprobe      network test client
third_party         ENet 1.3.18 (MIT), SQLite 3.53.4 (public domain), vendored source
tests               host unit tests (run by every build)
docs                this documentation
```
