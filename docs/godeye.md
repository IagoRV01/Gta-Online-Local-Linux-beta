# GodEye (Phase 9+)

Built so far (running it: [../godeye/README.md](../godeye/README.md)):
- **Phase 9, GodEye Basic:** a live 2D map of the players on the LAN.
- **Phase 10, GodEye Control:** logged-in users act on the game: wanted, police alerts, roadblocks, traffic lights,
  weather, time, blackout, messages.
- **Phase 11, 3D map:** the real map, Cayo Perico and our own islands, streamed from the player's own game files.

- **Phase 12, Camera Client:** a second GTA on the server PC, logged in as a camera, showing what GodEye picks.
- **Phase 13, live video:** the camera client's picture in the browser (WebRTC on the LAN, HLS over the internet).
- **Phase 14, web game:** Hack Credits, hacking minigames, daily limits, bounties and OPERATOR events.
- **GodEye.exe** (open and close from the desktop, on one chosen GPU) and an optional **public address**
  (Cloudflare Tunnel): [../godeye/README.md](../godeye/README.md).

All the phases of the brief are built.

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
  - Tile format: "GEM1", a JSON header, 14-byte vertices (u16 position, f16 uv, i8 normal) and u16 indices per
    texture batch. The position box is the file's own vertices, not the tile: one SLOD model can cover a whole
    district.
  - Until 2026-10-23 that box was the tile ±64 m, and 166 tiles had vertices squashed against its edges: stretched
    ground, a cut Wuhu.
- **Shader families:**
  - Shader hashes are read as names with CodeWalker's list (`ShaderNames.cs`).
  - Each batch gets a kind: solid, `cut` (alpha-tested), `decal` (alpha-tested, pulled forward), `tree`, `glass`
    (translucent), `emit` or `water`.
  - `emit` is the window-light panels laid over the walls. They are drawn as added light: faint by day, full at night,
    off in a blackout.
  - Shadow proxies are dropped.
  - `trees_lod2` billboards are only a centre point plus size and corner data, which the game's vertex shader turns
    to the camera. The exporter makes two crossed upright cards from them.
  - `GodEyeMap <gta> shaders3d <box>` lists shaders, vertex layouts and model reach for an area.
- **Textures:** each one's mip of at most 128 px, BC1-encoded (1-bit alpha), decoded by the browser (about 70 MB).
- **Files per tile** (index.json version 2, `packs: true`):
  - `far.bin` without its trees;
  - `tree.bin`, only loaded when the camera is within ~2.5–5 km;
  - `near.bin`;
  - a texture pack each for far+trees and for near: "GET1", the count, (key, length) pairs, then the BC1 files.
  - Every file also has a Brotli copy (`.br`, quality 9) made by the exporter.
  - The backend serves the `.br` copy when the browser takes br, gzip on the fly on the LAN, and nothing compressed
    to the PC's own window.
  - Files carrying the `?v=` stamp are `public, max-age=31536000, immutable`.
- **Edge cache:**
  - The Worker keeps those files in Cloudflare's cache while GodEye says the 3D map is open to everyone (asked at
    most once a minute).
  - After the first visit they come from Cloudflare instead of the home connection.
  - Cloudflare's cache stores them decoded, so hits travel uncompressed.
- **Measured 2026-10-23 on the public address with a warm cache:**
  - the overview filled 10 tiles in 5 s, 40 in 9 s and 80 in 16 s;
  - it took 198 requests. Before, it was thousands of single textures and ~130 MB per load through the home
    upload.
- **Viewer** (`godeye/frontend/map3d.js`, Three.js r186 vendored, MIT):
  - Orbit camera: drag to turn, right-drag to move, wheel or pinch to zoom.
  - Tiles stream by distance (on a timer, so loading goes on in a background tab).
  - Sea plane at 0 and the raised water of the water files.
  - **The sky follows the game:**
    - a dome, sun, ambient light and fog follow the server's clock and weather (`worldNow` in app.js);
    - rain, thunder and fog grey the sky and bring the fog closer;
    - neutral tone mapping, anisotropic filtering, alpha-to-coverage on cut-outs.
  - **Frames are drawn on demand:**
    - only when the camera, a tile, a texture, the sky or a player changed;
    - players alone redraw at most 20 times a second;
    - the pixel ratio is capped at 1.5.
    - Before, every frame redrew some 4,000 draw calls 60 times a second, even on a still view. Measured: 0 frames
      in 5 s on a still view.
  - The sky dome takes a gradient texture, so its faces no longer show.
  - The depth buffer is reversed where the GPU has EXT_clip_control, otherwise logarithmic. The logarithmic one
    writes depth per pixel and turns off early depth testing. Decals are pulled forward by the slope factor only.
  - The game's fog lies beyond the point looked at (near + 0.9 × camera distance), so FOGGY hazes the map instead of
    hiding it.
  - The 2D map also draws only when something changed.
  - A backdrop under everything: the lowest ground of each 25 m cell (`zmin` in map.json) 3 m lower. The few spots the
    far models leave open show earth, not the sea, and it never rises above real ground.
  - Tile and texture URLs carry the export's `built` stamp, so a new export is never mixed with cached old tiles.
- North Yankton (prologue ymaps) is left out: it sits where Cayo Perico is, and the game loads only one of them.
  - Players: a pin, a heading arrow and a name, interpolated like the 2D map. Picking a player follows them.
- **Verified 2026-10-23:**
  - Downtown up close shows real facades, round tree canopies, and no floating black panels or flat dark squares.
  - At 23:00 the windows light up across the city.
  - Wuhu is whole.
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

## Phase 13: live video

- **Pipeline:**
  - FFmpeg 8 `gfxcapture` (Windows Graphics Capture) on the camera window, a regex of its title;
  - `hwdownload`, then `h264_nvenc` (baseline, no B-frames, `-tune ll -zerolatency`, GOP = 1 s, 4 Mbit/s, 720p30);
  - RTSP to mediamtx on 127.0.0.1:8554;
  - mediamtx serves WebRTC (WHEP, 127.0.0.1:8889; media on UDP 8189) and low-latency HLS (127.0.0.1:8888, 1 s
    segments, 200 ms parts, always remuxed). MoQ, RTMP, SRT and the API are off. Publishing and reading are allowed
    from 127.0.0.1 only.
- **GPU:**
  - The encode runs on the card named in `video.gpu`. godeye/backend `pickGpuUuid` matches it in
    `nvidia-smi --query-gpu=index,name,memory.total,uuid` and passes its UUID in `CUDA_VISIBLE_DEVICES` to that FFmpeg
    only.
  - The capture itself happens on the card that composites the desktop (the one with the monitor). That is
    inherent to Windows Graphics Capture.
- **Backend** (`godeye/backend/src/video.ts`):
  - It starts and restarts mediamtx and one FFmpeg per stream.
  - A stream is live once FFmpeg reports frames; 12 s without a new frame restarts the capture.
  - `/api/video` lists the streams.
  - `/video/<path>/whep[/<session>]` relays WHEP (POST offer / PATCH / DELETE, `Location` rewritten). Non-GET
    requests must come from the same origin.
  - `/video/<path>/index.m3u8` and its parts relay HLS, and `/video/<path>/hls.min.js` relays the hls.js that
    mediamtx ships.
  - CSP: `media-src 'self' blob:; worker-src 'self' blob:`.
- **Page** (`godeye/frontend/video.js`):
  - The LIVE button opens a panel over the map, labelled with what the camera shows (the camera telemetry).
  - On a LAN or loopback address it tries WebRTC first (6 s to connect), else HLS: native where the browser has it,
    else hls.js.
- **Verified 2026-10-23:**
  - A window on virtual desktop 2: about 21 fps captured; encoded on the RTX 5060 Ti 8 GB (nvidia-smi shows the
    FFmpeg CUDA context on GPU 0).
  - WebRTC 1280×720 playing in the browser.
  - HLS through hls.js about 2.2 s behind.
  - The camera GTA's own window (the landing page) live at https://godeye.goricito.cc over HLS through the tunnel.

## Phase 14: the web game

- **Files:**
  - `godeye/backend/src/game.ts`: credits, prices, limits, bounties, events;
  - `godeye/backend/src/puzzles.ts`: the minigames;
  - `godeye/frontend/game.js`: the page.
- **The flow of a priced action** (`POST /api/action`):
  1. role → checked values (`toCommand`) → cooldown;
  2. `game.quote`: the price (× the event's price factor), today's uses against `daily`, the balance;
  3. when `puzzles[action]` applies and nothing is being put back, the answer is **202 `{puzzle, price}`**;
  4. the page plays it and sends the same action again with `puzzle: {id, answer}`;
  5. `Puzzles.solve` takes the puzzle out (single use), checks the user, the exact command line and the time, then the
     answer;
  6. wrong or late: **403 ACCESS DENIED**, and `Limits.block` locks the action for 15 s;
  7. then the game server command. Only if it is accepted: `game.charge` (and `addBounty` for "+n wanted").
- **Puzzles:**
  - `sequence`, three levels: +k; +p+q, ×m, +d+2d+3d; a+b, ×m+c, squares, with a middle term hidden. Four options.
  - `route`: a 4×4 to 5×5 grid on a random spanning tree plus extra links, with 2–4 locked nodes. The generator
    rejects grids with no way through, or a shortest way shorter than the width. The answer must be a simple path of
    real links from IN to OUT.
  - The browser never gets an answer.
- **Ledger** (`game.ledgerFile`, JSON lines):
  - `{at,user,delta,why,action?,day?}` for every balance change: why = start, daily, income, bounty, grant by X, or
    the action;
  - `{t:"event",…}` and `{t:"eventEnd",id}`.
  - Each line is `fsync`ed. A damaged last line (power cut) is skipped at load.
- **Income:** a minute timer pays `incomePerMinute` to users whose page asked `/api/game` in the last 2 minutes (the
  page polls every 15 s), only while a player is in the world.
- **Bounties:** in memory. Each telemetry update compares the target's `deaths` with the value when the stars were
  bought.
- **Events:**
  - `EVENTS` in game.ts: start commands, end commands, length, and optionally a target, `priceX` or `bountyX`.
  - A 1 s timer runs the end commands once `endsAt` passes and the game server is connected. That includes an event
    that was running when GodEye stopped.
  - `event stop` (the starter or ADMIN) sets `endsAt` to now.
  - Browsers get `{t:"game",event}` and `gameEvent` in the full state.
- **API:**
  - `GET /api/game`: credits, prices, limits left, puzzle actions, events, the running event, the top-10 board.
  - `POST /api/game/grant {user, amount}`: ADMIN only, ±100000.
- **Verified 2026-10-23** against the isolated test server, with a netprobe player:
  - the e2e (scratchpad `e2e14.mjs`) passes 30 of 30;
  - balances and daily uses are identical after a GodEye restart;
  - an Electrical storm survived a restart mid-event, and stopping it brought the power back;
  - the page (desktop and 375 px) showed both minigames, ACCESS GRANTED, the event banner, halved prices and the
    board;
  - 21 unit tests pass.

## Accounts and the control panel

- **Accounts** (`auth.ts`):
  - `POST /api/register {name, password, role}` makes an account and logs it in.
  - With no account yet, the first one must come from the LAN (not the public address) and is the ADMIN, whatever
    role it asked for.
  - After that it needs sign-up to be open, and a role from `settings.signupRoles`.
  - 3 accounts an hour per address, 1,000 at most.
  - `/api/me` tells the page `setup` (no account yet, LAN), `signup` (the roles offered) and `panel` (ADMIN).
- **Settings** (`panel.ts` `SettingsStore`, `settings.json`): `registration`, `signupRoles` and `publicMap3d`, which
  start from godeye.json.
- **Control panel** (ADMIN):
  - `GET /api/panel`: the PC (CPU from core times, memory, nvidia-smi every 3 s, disk space), the game server,
    GodEye, settings, accounts, credits and the latest actions.
  - `GET /api/panel/logs?which=godeye|server`: the last 250 lines.
  - `POST /api/panel/user {op: create|role|password|delete}`. A role change reaches open sessions. A new password or
    a deletion ends them. The last ADMIN stays.
  - `POST /api/panel/settings`.
- **Verified 2026-10-23:**
  - `panel_e2e.mjs`: 24 of 24.
  - A fresh install: the public address is refused; the first LAN account becomes ADMIN.
  - 27 unit tests.
  - The panel showed both RTX 5060 Ti cards, the CPU and the memory live.

## Security baseline

- Accounts and roles: HACKER / DISPATCH / OPERATOR / ADMIN.
- Session cookies (HttpOnly, SameSite), CSRF protection on POSTs, per-user rate limits, an audit log in the `events` table.
- The admin API binds to loopback only, with a shared secret from `server.toml`.

## Open items

- **Traffic lights:** done as our own control layer (Phase 10 above). The AI does not follow the props, so stops are
  approximated from the light poles. Exact stop lines from the path nodes' traffic-light junctions (1,235 in map.json)
  would be better.
- **Wanted:** in multiplayer, prefer a **server-owned custom wanted level** (★ alert … ★★★★★ SWAT). The server spawns and assigns police through the ownership system and mirrors it to the native wanted HUD on the owning client. GTA's native wanted level is per-instance and can't be shared consistently.
