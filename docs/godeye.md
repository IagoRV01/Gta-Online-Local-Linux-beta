# GodEye (Phase 9+)

**Phase 9 (GodEye Basic) is built**: a live 2D map of the players on the LAN. Running it: [../godeye/README.md](../godeye/README.md).
Phases 10–14 (controls, 3D map, Camera Client, WebRTC, web game) follow the design below.

## Placement

- GodEye runs on the Windows PC as a **separate Node.js/TypeScript process**. Node 24 is already installed.
- It serves the web app over HTTP and **WebSocket on TCP 8080** (`[godeye] http_port`).

## Data flow

```
GTA clients ──ENet──► Game server ──local admin API (127.0.0.1 only)──► GodEye backend ──WS 5–10 Hz──► browsers (interpolate at 60 FPS)
```

- The game server publishes a **telemetry view** of its authoritative state: players, positions, heading, vehicle, speed, health, armour, wanted, zone, online state.
  - It is pushed to GodEye at 10 Hz, with changes only.
  - GodEye never talks to GTA.
- Control actions (traffic, wanted, weather, blackout…) go:
  browser → GodEye (authN, role check, credits, cooldown, rate limit, validation) → game server **admin command** (re-validated against game rules) → clients.

## Phase 9: what exists

- **Game server admin API** (`server/game-server/src/admin_api.cpp`).
  - TCP on 127.0.0.1:`godeye.admin_port` (4500), newline-delimited JSON. Polled from the server's tick (20 Hz), non-blocking.
  - First line `{"t":"auth","secret":…}`. The secret comes from `godeye.admin_secret`, or the file `godeye.admin_secret_file`
    (default `admin.secret` beside the DB), created with 48 random hex characters on the first start. Wrong secret or
    no auth within 5 s: closed and logged. At most 4 connections, 4 KB lines, 1 MB unsent output.
  - `{"t":"hello",server,protocol,tick,telemetry}`, then `{"t":"tel",ms,world?,players:[…],gone:[netId]}` at
    `godeye.telemetry_hz` (10). A player is sent only when its rounded line changed (0.1 m, 1°, 0.1 m/s): a player
    standing still costs nothing. `{"t":"full"}` resends everything.
  - Player fields: `id name rank session passive activity kills deaths playing`, and when playing (YOU, in the world)
    `x y z h spd hp ar wanted weapon dead move veh{model,seat,driver}|null`. World: `hour minute frozen weather next auto blackout`.
  - `{"t":"cmd","id":n,"line":"weather RAIN"}` runs a server.cmd command (`time`, `freeze`, `weather`, `blackout`) and
    answers `{"t":"cmdr","id":n,"ok":bool}`. Not exposed to browsers yet (Phase 10).
- **GodEye backend** (`godeye/backend`, Node 24 running the TypeScript directly, no packages).
  - Names: zone from the game's own `popzone.ipl` boxes (smallest box holding the point), vehicle from its model
    hash (881 models), weapon from its hash.
  - Browsers: HTTP + WebSocket on 8080, private networks only, same-origin WebSockets, CSP. Forwards each telemetry
    delta as it comes (≤ 10 Hz); the full state on connect.
- **Web app** (`godeye/frontend`).
  - Canvas map: terrain (25 m grid with hillshade), roads by class, zone names, traffic-light stops.
  - Players drawn 150 ms behind the newest sample and interpolated every frame. Arrow on foot, car shape in a
    vehicle. Name, wanted stars, a one-minute trail.
  - Panel: zone, position, heading, speed, vehicle and seat, weapon, wanted, K/D, health and armour, online state.
  - Pan, zoom (wheel or pinch), follow a player, fit everyone. Works at phone width.
- **Map data** (`tools/godeye-map`, CodeWalker): made from the user's own game files into `godeye/data/map.json`,
  which is git-ignored.
  - `heightmap.dat` + `water.xml`: terrain, sea and lakes (Alamo Sea, reservoirs).
  - `paths.rpf` nodes: 71k road segments; boat routes left out.
  - `popzone.ipl`: zones.
- **Verified 2026-10-22** on an isolated server (UDP 4599, temp DB) with two netprobe players.
  - On foot at Legion Square: zone "Legion Square", 14 km/h. Driving a Sultan round LSIA: "Sultan (Sports), driver", 108 km/h.
  - Admin `time 22 15` / `weather RAIN` / `blackout on` reached the page within 2.5 s.
  - Refused: a wrong secret (closed, logged), a WebSocket from another origin (403), path tricks (404).
- **Then on the live server with the BC-250's GTA:** see the roadmap.

## Security baseline

- Accounts and roles: HACKER / DISPATCH / OPERATOR / ADMIN.
- Session cookies (HttpOnly, SameSite), CSRF protection on POSTs, per-user rate limits, an audit log in the `events` table.
- The admin API binds to loopback only, with a shared secret from `server.toml`.

## Open research items (before Phase 10)

- **Traffic lights:** test `SET_ENTITY_TRAFFICLIGHT_OVERRIDE` on light props found near configured intersections. If it is unreliable, build our own control layer for the enabled intersections (stop lines plus scripted vehicle behaviour).
- **Wanted:** in multiplayer, prefer a **server-owned custom wanted level** (★ alert … ★★★★★ SWAT). The server spawns and assigns police through the ownership system and mirrors it to the native wanted HUD on the owning client. GTA's native wanted level is per-instance and can't be shared consistently.
