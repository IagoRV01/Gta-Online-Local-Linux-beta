# GodEye (Phase 9+)

Built so far (running it: [../godeye/README.md](../godeye/README.md)):
- **Phase 9, GodEye Basic:** a live 2D map of the players on the LAN.
- **Phase 10, GodEye Control:** logged-in users act on the game: wanted, police alerts, roadblocks, traffic lights,
  weather, time, blackout, messages.
- **Phase 11, 3D map:** the real map, Cayo Perico and our own islands, streamed from the player's own game files.

- **Phase 12, Camera Client:** a second GTA on the server PC, logged in as a camera, showing what GodEye picks.
- **GodEye.exe** (open and close from the desktop, on one chosen GPU) and an optional **public address**
  (Cloudflare Tunnel): [../godeye/README.md](../godeye/README.md).

Phases 13–14 (WebRTC, web game) follow the design below.

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

## Phase 10: GodEye Control

- **Protocol v12.** `GodEyeAction` (41, server → client, reliable): `{u8 kind, u8 value, u32 durationMs, str text}`.
  Kinds: wanted (value 0-5), police alert (text), roadblock, traffic (value TrafficMode, duration), notice (text).
  - It goes to the target player's client only, except traffic, which goes to everyone.
  - The traffic mode also goes to anyone who logs in while it lasts, and the server puts it back to normal when the
    time runs out.
- **Server admin commands** (server.cmd and the admin API):
  - `wanted <netId> <0-5|+n|-n>`, `alert <netId> [text]`, `roadblock <netId>`;
  - `notify <netId|all> <text>`, `traffic red|green|random|normal [seconds]`.
  - The player must be in the world (YOU, not story mode). Each one is logged and stored as a `godeye` event.
- **Client** (`client/gta-plugin/src/game/godeye.cpp`), all fiction inside that player's own game:
  - **Wanted:** the native wanted level, set or cleared.
  - **Police alert:** a dispatch notification and flashing stars. A police cruiser with two cops is sent from a road
    120-300 m away to where the player is; they get out on arrival. Released after 150 s.
  - **Roadblock:** two police cars across the road 140 m ahead of the player (along their movement), four armed cops
    guarding behind them, and a spike strip in front that bursts the tyres of the player's car. Released after 180 s or
    650 m away.
  - **Traffic lights:** GTA's AI ignores what a light prop shows, so this is our own control layer within 160 m of
    the player:
    - the props are overridden (`SET_ENTITY_TRAFFICLIGHT_OVERRIDE`: red 1, green 0, random 0-2 every 2-7 s, normal 3);
    - ALL GREEN gives the drivers a driving style without "stop at lights";
    - ALL RED brakes every car that has a light pole 6-28 m ahead;
    - RANDOM has half the drivers ignore lights.
  - **Leaving and going offline:** everything is released and the lights go back to the game's cycle.
- **Gori:** "lost the cops" RP is paid only after a real escape (the stars greyed out within 3 s before they went).
  Otherwise GodEye clearing a friend's stars would be an RP farm.
- **GodEye backend:**
  - Users in `users.json` (`node src/users.ts add <name> <role>`): scrypt hashes, sessions in memory.
  - Session cookie: HttpOnly, SameSite=Strict, 12 h.
  - Every POST must come from the same Origin and echo the session's CSRF token.
  - Logins: 5 failures per address in 5 minutes, then a wait.
  - Roles (ADMIN can do everything):
    - DISPATCH: wanted, alert, roadblock;
    - HACKER: traffic, blackout;
    - OPERATOR: weather, time, blackout, messages.
  - Per user: a cooldown per action (10-60 s, a fifth for ADMIN) and 20 actions a minute.
  - Inputs are checked against fixed lists before they become a server command, and the server checks them again.
  - The audit log (`/api/audit`) and a live event feed for every viewer.
- **Web:** a login dialog and a Control panel that shows only what the role may do, with cooldown countdowns.
  Police actions target the player picked in the list.
- **Verified 2026-10-22/23:**
  - On an isolated server, 20 end-to-end checks passed:
    - roles, CSRF, origin, cooldowns, validation;
    - every action accepted by the game server;
    - the world state changed accordingly.
  - In game on the BC-250:
    - wanted +2: the HUD showed 2 stars and GodEye read wanted 2;
    - police alert: the unit came from 121 m and reached the player;
    - roadblock: two cruisers across the road, cops behind, a car stopped in front;
    - ALL RED: 8 lights overridden, about 22 forced stops every 5 s;
    - ALL GREEN: drivers restyled;
    - NORMAL: everything restored.

## Phase 11: the real map in 3D

- **Made locally** by `tools/godeye-map map3d` from the player's own game files: the base game, update, every DLC
  pack (Cayo Perico included) and the extra packs given on the command line (Isla Neón, Wuhu).
  - A folder of loose ymaps can replace the game's own: the patched Hawick, road and freeway ymaps copied from the
    BC-250's mods/.
  - The output (`godeye/data/map3d`, about 1 GB) is git-ignored and never published.
- **The game's own LOD models**, in 1 km tiles:
  - far: the SLOD1 models, the LOD models with no SLOD1 above, and the SLOD2/3/4 models 4/8/12 m lower (the
    distant hills SLOD1 leaves out);
  - near: a tile's LOD models, which replace its SLOD1 ones when the camera is within 1.5 km;
  - our islands' models: drawn as they are (they have no LODs).
  - Tile format: "GEM1", a JSON header, 14-byte vertices (u16 position in the tile box, f16 uv, i8 normal) and u16
    indices per texture batch.
- **Textures:** each one's mip of at most 64 px, BC1-encoded (1-bit alpha), decoded by the browser.
- **Viewer** (`godeye/frontend/map3d.js`, Three.js r186 vendored, MIT):
  - Orbit camera: drag to turn, right-drag to move, wheel or pinch to zoom.
  - Tiles stream by distance (on a timer, so loading goes on in a background tab).
  - Sea plane at 0 and the raised water of the water files.
  - A backdrop under everything: the lowest ground of each 25 m cell (`zmin` in map.json) 3 m lower. The few spots the
    far models leave open show earth, not the sea, and it never rises above real ground.
  - Tile and texture URLs carry the export's `built` stamp, so a new export is never mixed with cached old tiles.
- North Yankton (prologue ymaps) is left out: it sits where Cayo Perico is, and the game loads only one of them.
  - Players: a pin, a heading arrow and a name, interpolated like the 2D map. Picking a player follows them.
- **Verified 2026-10-23:**
  - Overview of Los Santos: 44 far tiles, 5.8 M triangles, 3,400 draw calls.
  - Close to Alex at Hawick: 6 near tiles swapped in, 3 M triangles, 767 calls, buildings and trees textured.

## Phase 12: the Camera Client

- **Protocol v13.** `Auth` carries a role (0 player, 1 camera). `CameraSet` (42, server → camera) carries:
  `{u8 mode, u16 target, str cameraId, vec3 pos, vec3 rot, f32 fov, str label}`.
  - Modes: cctv, follow, heli, drone, cinematic, third, chase, free.
- **Server**
  - A camera client never appears to the players: no PlayerJoined/Left, no info relay, no snapshot entry.
  - It gets every player, in every session.
  - At most `godeye.max_cameras` (4) at a time; they do not count against `max_players`.
  - CCTV presets come from `cameras.json` (`godeye.cameras_file`), else four defaults.
  - `camera <id|all> cctv <CAM_ID>` | `camera <id|all> <mode> <player netId>`.
  - A camera on a player who leaves goes back to the first CCTV camera.
  - Telemetry: `cameras: [{id, name, mode, target, cctv, label}]` when it changes; the hello lists the CCTV presets.
- **Client** (`client/gta-plugin/src/game/camera_client.cpp`, `[camera] enabled = true` in client.toml):
  - No start screen and no character. The story ped is hidden, frozen, invincible and ignored by everyone, and wanted
    level is off.
  - HUD and controls off.
  - Puppets of every player, the server's clock and weather.
  - A script camera per mode: follow, third, chase (deeper when in a vehicle), heli (70 m orbit, 55 m up), drone
    (18 m orbit), cinematic (a cut every 8 s: low front, side dolly, high orbit, low behind).
  - The world streams round the camera: SET_FOCUS_POS_AND_VEL, and the hidden ped is moved along.
  - A REC label with the camera name and the game clock.
- **GodEye**
  - The `camera` action is for OPERATOR and HACKER (as in the brief), with a 2 s cooldown.
  - The Cameras panel shows the camera clients and what each shows. It has the CCTV buttons from the server, and
    modes for the selected player.
- **On the user's PC**
  - GTA's AdapterIndex is 1, the RTX 5060 Ti 8 GB as DXGI numbers it (the 16 GB card appears three times).
  - 1280×720 window. `camera_client.py` launches it through Steam with `-scOffline` and keeps it on virtual desktop 2.
- **Verified 2026-10-23** on an isolated server, with a netprobe camera and a netprobe player:
  - the camera got CAM_001 by default and saw the player;
  - it followed FOLLOW / HELI / CAM_003 on command; an unknown target was refused;
  - the player saw 0 other players.

## Security baseline

- Accounts and roles: HACKER / DISPATCH / OPERATOR / ADMIN.
- Session cookies (HttpOnly, SameSite), CSRF protection on POSTs, per-user rate limits, an audit log in the `events` table.
- The admin API binds to loopback only, with a shared secret from `server.toml`.

## Open items

- **Traffic lights:** done as our own control layer (Phase 10 above). The AI does not follow the props, so stops are
  approximated from the light poles. Exact stop lines from the path nodes' traffic-light junctions (1,235 in map.json)
  would be better.
- **Wanted:** in multiplayer, prefer a **server-owned custom wanted level** (★ alert … ★★★★★ SWAT). The server spawns and assigns police through the ownership system and mirrors it to the native wanted HUD on the owning client. GTA's native wanted level is per-instance and can't be shared consistently.
