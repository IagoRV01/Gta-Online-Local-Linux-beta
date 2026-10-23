# GodEye

The web view of GTA Local Online. It runs on the Windows server PC next to `LocalOnlineServer.exe`.
- **Phase 9 (GodEye Basic):** a live 2D map of the players.
- **Phase 10 (Control):** logged-in users act on the game by role: police, traffic lights, power, world, messages.
- **Phase 11 (3D):** the real map in 3D, streamed from tiles made from your own game files.
- **Phase 12 (cameras):** a second GTA on the server PC as a camera client; GodEye picks what it shows.
- **Phase 13 (live video):** that camera's picture in the browser: WebRTC on the LAN, HLS from the internet.
- **Phase 14 (web game):** people without GTA play too: Hack Credits, hacking minigames, daily limits, bounties, events.
- **GodEye.exe:** opens and closes everything from the desktop, on the graphics card you choose.
- Design and the later phases: [../docs/godeye.md](../docs/godeye.md), [../docs/cameras.md](../docs/cameras.md).

```
GTA clients ──ENet──► LocalOnlineServer.exe ──admin API, 127.0.0.1:4500──► GodEye backend ──WebSocket :8080──► browsers
```

| Folder | What |
|---|---|
| `backend/` | Node.js 24, TypeScript run directly (`node src/main.ts`), no npm packages. Reads the server's telemetry, adds zone/vehicle/weapon names, pushes changes to browsers, serves the web app. |
| `frontend/` | The web app: canvas map (terrain, roads, zone names, traffic lights), player markers interpolated at 60 FPS with a one-minute trail, player panel. Works on phones. |
| `data/map.json` | **Made on your PC from your own GTA files, never committed.** Without it the map is a plain grid. |
| `camera/` | Phase 12: starts and stops the camera GTA (`camera_client.py`). |
| `video/` | Phase 13: the mediamtx config for the live video (WebRTC on the LAN, HLS over the internet). |

## Running it

1. **Start the game server.** From server 0.3.0 on, it opens the admin API on 127.0.0.1:4500.
   - The first start creates the shared secret, `admin.secret`, next to the database.
2. **Make the map data once** (and again after a GTA update) with `tools/godeye-map`. It needs the .NET 8 SDK and a CodeWalker checkout (MIT), which reads the game files:
   ```
   cd tools\godeye-map
   dotnet build -c Release -p:CodeWalkerDir=C:\path\to\CodeWalker
   dotnet bin\Release\net8.0\GodEyeMap.dll "C:\...\Grand Theft Auto V" ..\..\godeye\data\map.json
   ```
   This writes about 2 MB:
   - terrain on a 25 m grid, from `heightmap.dat` and `water.xml`;
   - 71k road segments from the path nodes, in 4 classes;
   - 1,235 traffic-light stops;
   - the zone boxes from `popzone.ipl`.
3. **Configure GodEye.** Copy `backend/godeye.example.json` to `backend/godeye.json` and set `admin.secretFile` to the server's `admin.secret`.
4. **Start it.** Run `server\windows\start-godeye.bat`, or `node src/main.ts godeye.json` inside `backend/`.
5. **Open it.** Browse to `http://<server LAN IP>:8080` on any PC or phone on the LAN.
   - Other devices need an inbound TCP 8080 rule in Windows Firewall. That is your decision; see `server/windows/setup-firewall.ps1`.
6. **Make users for the controls** (Phase 10), on the server PC, in `backend/`:
   ```
   node src/users.ts add <name> ADMIN
   ```
   It asks for the password and does not echo it. The roles are HACKER, DISPATCH, OPERATOR and ADMIN.
   - Without users the page is view-only.
   - GodEye reads `users.json` when it starts.
7. **Make the 3D map** (Phase 11), once and again after a GTA update. Add the paths of your own map packs, and a
   folder of patched ymaps if your game edits some:
   ```
   dotnet bin\Release\net8.0\GodEyeMap.dll "C:\...\Grand Theft Auto V" map3d ..\..\godeye\data\map3d [dlc.rpf...] [ymap folder]
   ```
   - It takes about 5 minutes and writes about 1 GB, never committed.
   - The 3D button on the page uses it.

`npm test`, or `node --test "test/*.test.ts"` in `backend/`, runs the unit tests.

## GodEye.exe: open and close it, on the graphics card you choose

`godeye/launcher` builds `GodEye.exe` (`./build.sh godeye`). Put it wherever you like, e.g. on your desktop.
- **Opening it** starts the GodEye backend (and the public tunnel, if configured), then opens the GodEye window.
- **Closing the window** stops what it started. If the backend already runs as a service, it is left alone.

**Settings:** `GodEye.ini` next to the exe, or `%LOCALAPPDATA%\GodEye\GodEye.ini`. Start from
[launcher/GodEye.example.ini](launcher/GodEye.example.ini).
- If it cannot find or read the file, it says where it looked and what Windows answered.
- Keep GodEye's folder (the exe, `GodEye.ini`, cloudflared, mediamtx, FFmpeg) somewhere plain, e.g. `D:\GodEye`, and
  put a shortcut on the desktop.
- Apps installed from the Store or as MSIX packages see a private copy of `AppData`. Files they write there are not
  where a double-clicked GodEye.exe looks for them, so set GodEye up from a normal Explorer or terminal window.
- GodEye stays on while any process of its window is alive: Edge sometimes hands the window to a new process (on a
  fresh profile, for instance), and that no longer closes GodEye.

**Choosing the graphics card.** GodEye draws on one card only: the 3D map, and later the camera client and the
encoder.
1. List the cards Windows sees:
   ```
   GodEye.exe --gpus
   ```
   It prints one line per adapter: `index:1  NVIDIA GeForce RTX 5060 Ti  8 GB  luid:0,63810`.
2. Put one in `GodEye.ini` as `gpu=`:
   - `gpu=RTX 5060 Ti 8GB`: words of its name plus its memory. Use this when you have two cards of the same model.
   - `gpu=luid:0,63810`: that exact adapter. The LUID can change after a driver update; the name form does not.
   - `gpu=auto`: Windows decides.
3. With a card named there, GodEye refuses to open on any other and lists the ones it found.

How it works: the window is Edge (or Chrome) in app mode with its own profile, started with
`--use-angle=d3d11 --use-adapter-luid=<high>,<low>`. Chromium then renders on that adapter.
- Check it with `nvidia-smi`: the `msedge.exe` GPU process appears on that card.
- With your monitor on another card, the finished frame is also presented there. That is normal.

The camera GTA picks its card in its own settings (below).

## Zorua (optional art)

GodEye's mascot is Zorua, as community sprites from [PMDCollab SpriteCollab](https://github.com/PMDCollab/SpriteCollab).
- **License:** CC BY-NC 4.0. The artists are baronessfaron, Emmuffin and MilesFarber (see
  [frontend/zorua/CREDITS.md](frontend/zorua/CREDITS.md)).
- Zorua is © Nintendo / Creatures / GAME FREAK, and GodEye is a non-commercial fan project.
- The files are in `godeye/frontend/zorua/`, with the launcher icons in `godeye/launcher/zorua*.ico`.
- To download them again from the pinned SpriteCollab commit, each checked against its SHA-256:
```
python tools/zorua/fetch_zorua.py
```
- **What it does:**
  - downloads 18 portraits and 2 animations from one pinned SpriteCollab commit, each checked against its SHA-256,
    into `godeye/frontend/zorua/`;
  - writes `manifest.json` and `CREDITS.md` there;
  - with Pillow installed, makes `godeye/launcher/zorua.ico` and `zorua-error.ico`.
- **Each moment has its own expression:**

  | Where | Zorua |
  |---|---|
  | Header | Idle animation; asleep while the game server is offline |
  | Favicon, GodEye.exe icon | Normal |
  | Launcher error dialogs | Worried |
  | Login, wrong password, welcome | Normal, Angry, Happy |
  | Role badge | HACKER: Hisuian Zorua (Determined); DISPATCH: Shouting; OPERATOR: Inspired; ADMIN: shiny Zorua |
  | Hacking minigame | Determined; Worried in the last 6 s |
  | Access granted, denied, too slow | Joyous, Pain, Dizzy |
  | Messages | no credits or daily limit: Sad; cooldown: Sigh; game server offline: Crying; other errors: Stunned |
  | Event banners | storm: Surprised; gridlock: Stunned; manhunt: Shouting; happy hour: Joyous |

- **Afterwards:**
  - rebuild GodEye.exe (`./build.sh godeye`) for the icons;
  - the page picks the sprites up on its next load. Without them it keeps the plain eye.
- **The public "GodEye is closed" page** shows a sprite only when `wrangler.toml` sets `OFF_SPRITE_URL`,
  `OFF_SPRITE_FRAMES` and `OFF_SPRITE_CREDIT` (see `wrangler.example.toml`).

## Public address (optional)

GodEye can be reached from the internet through a Cloudflare Tunnel while it is open. You need:
- a domain on Cloudflare;
- `wrangler` logged in;
- `cloudflared.exe` (from github.com/cloudflare/cloudflared, signed by Cloudflare).

1. Create the tunnel and the private route to GodEye:
   ```
   npx wrangler tunnel create godeye
   npx wrangler vpc service create godeye --type http --tunnel-id <tunnel id> --ipv4 127.0.0.1 --http-port 8080
   ```
2. Get the tunnel's token (Cloudflare API: `GET /accounts/<account>/cfd_tunnel/<id>/token`, or the dashboard). Save
   it in a file only you can read.
3. In `godeye/cloudflare`:
   - copy `wrangler.example.toml` to `wrangler.toml`;
   - set your hostname and the VPC service id;
   - run `npx wrangler deploy`.
4. In `GodEye.ini`, set `cloudflared=` and `tunnel_token_file=`. GodEye.exe then runs the tunnel while GodEye is open.
   While it is closed, the address shows "GodEye is closed".

**On the public address:**
- Anyone can watch the 2D map and the players.
- The 3D map needs a GodEye login: it is made of the game's own models and textures, which must not be published.
  `"publicMap3d": "open"` in godeye.json opens it, at your own risk.
- The controls always need a login.
- The session cookie is `Secure` there, and failed logins are limited per visitor IP.

## Camera client (Phase 12)

A second GTA V on the server PC that logs in as a **camera**:
- it has no character, and players never see it;
- it shows what GodEye picks: a CCTV camera (from the server's `cameras.json`), or a player in follow, third person,
  chase, helicopter, drone or cinematic.

Phase 13 streams its picture to browsers.

1. **Install LocalOnline.asi** in that GTA (ScriptHookV needed), and `LocalOnline\client.toml` with:
   ```
   [server]
   address = "127.0.0.1"
   [camera]
   enabled = true
   name = "Camera 1"
   ```
2. **Pick its graphics card** in `Documents\Rockstar Games\GTA V\settings.xml`:
   - `<AdapterIndex value="N" />`, where N is the `index:` that `GodEye.exe --gpus` prints for your card;
   - a window: `<Windowed value="1" />`, 1280×720.
3. **Start it:** `python godeye/camera/camera_client.py start` (`pip install pyvda`).
   - It launches GTA through Steam with `-scOffline`.
   - It moves its windows to virtual desktop 2, so whoever uses the PC is not interrupted.
   - `stop` closes it.
4. **Steam allows one game at a time per account.** While someone plays with the same account elsewhere, put this PC's
   Steam in offline mode, or use a second account that owns GTA V.

## Live video (Phase 13)

The camera client's window becomes live video in GodEye (the **LIVE** button, over the 2D or 3D map):
```
GTA window --FFmpeg gfxcapture--> NVENC H.264 (your chosen card) --RTSP--> mediamtx (127.0.0.1)
mediamtx --WebRTC (WHEP), ~0.2 s--> browsers on your LAN
mediamtx --low-latency HLS, ~2-4 s--> everyone else (the public address)
```
- **The software**, both downloaded from GitHub and left in their own folders:
  - [mediamtx](https://github.com/bluenviron/mediamtx) (MIT);
  - [FFmpeg 8.0 or newer](https://github.com/BtbN/FFmpeg-Builds) (GPL builds), for the `gfxcapture` source.
- **godeye.json:**
  ```
  "video": { "enabled": true, "mediamtx": "C:/.../mediamtx.exe", "ffmpeg": "C:/.../ffmpeg.exe",
             "gpu": "RTX 5060 Ti 8GB",
             "streams": [{ "path": "cam1", "name": "Camera 1", "window": "^Grand Theft Auto V$" }] }
  ```
  - `gpu` picks the NVIDIA card NVENC encodes on: words of its name plus its memory (as `nvidia-smi` shows it),
    `uuid:GPU-...`, or `auto`. GodEye sets `CUDA_VISIBLE_DEVICES` for its FFmpeg only.
  - With two cards of the same model, the card order inside CUDA programs differs from nvidia-smi's, so a bare index
    would pick the wrong one.
- **The GodEye backend does the rest**:
  - starts mediamtx with [video/mediamtx.yml](video/mediamtx.yml) (everything on 127.0.0.1);
  - starts FFmpeg on the window, retrying every 10 s while it does not exist, and restarting it when it stops
    delivering frames;
  - relays WHEP and HLS on its own origin, at `/video/<stream>/...`.
- **Windows Graphics Capture** records the window even while it sits on another virtual desktop.
- **Firewall:** viewers on other LAN devices need UDP 8189 for WebRTC; without it they fall back to HLS. Run
  `setup-firewall.ps1 -GodEye`.
  - The first time mediamtx starts, Windows asks whether MediaMTX may use the network. Answer it once, either way.
  - While that prompt is open, its dimmed screen covers every window, so the camera client cannot click STORY
    MODE. It says so instead of clicking.
- **Public address:** the video is HLS through the tunnel (WebRTC media cannot cross it), so it is a few seconds
  behind.

CCTV cameras live in `cameras.json` next to `LocalOnlineServer.exe`:
```
{"cameras": [{"id": "CAM_001", "name": "Legion Square", "zone": "LEGSQU", "pos": [172, -950, 48], "rot": [-25, 0, 323], "fov": 55}]}
```
- Without the file there are four defaults: Legion Square, Airport, Vinewood, Vespucci.
- In GodEye, OPERATOR and HACKER (and ADMIN) get a **Cameras** panel: pick a camera client (or all), then a CCTV
  camera, or a mode for the selected player.

## The web game (Phase 14)

GodEye users play against the people in GTA, without GTA. All of it is fictional and stays inside GodEye and the game
server.

- **Roles**:
  - HACKER: traffic lights, power, cameras.
  - DISPATCH: wanted level, police alerts, roadblocks.
  - OPERATOR: world, cameras, messages, events.
  - ADMIN: everything.
- **Hack Credits:** every action has a price. Putting things back is free: lights on, normal traffic, clearing stars.

  | Action | Credits |
  |---|---|
  | Traffic hack | 50 |
  | Police alert | 100 |
  | +1 wanted | 150 per star |
  | Roadblock | 250 |
  | Blackout | 500 |
  | Weather, time | 25 |
  | Message | 10 |

- **Earning credits:**
  - 500 to start;
  - a 250 bonus on the first visit of each day;
  - +10 a minute while the page is open and someone is in the world;
  - **bounties:** when DISPATCH raises someone's wanted level and that player dies within 3 minutes, DISPATCH earns 1.5×
    what it paid.
  - An ADMIN can also give or take credits (the form under Hack Credits).
- **Hacking minigames** stand before the bigger hacks:
  - a number sequence (`POLICE NETWORK · 4 → 7 → 10 → ?`) for traffic and wanted;
  - a node route around firewalls (`TRAFFIC GRID`) for roadblocks and blackouts.

  The backend makes each puzzle and keeps its answer. A puzzle has a timer, is single-use, and is bound to that user and
  that exact action. A wrong answer is ACCESS DENIED and locks that action for 15 s. Nothing is charged until the
  game server accepts the action.
- **Daily limits** per user: 3 blackouts, 5 roadblocks, 10 wanted, 10 traffic hacks, 10 alerts and 5 events.
- **Cooldowns:** the Phase 10 ones still apply.
- **Events (OPERATOR):** one at a time, with a banner over the map for every viewer.

  | Event | Credits | Length | Effect |
  |---|---|---|---|
  | Electrical storm | 600 | 5 min | thunder and a citywide blackout |
  | Gridlock | 400 | 3 min | every light red |
  | Manhunt | 500 | 5 min | 3 stars and an alert on the selected player; bounties pay double |
  | Happy hour | 300 | 10 min | every hack half price |

  When the time is up, GodEye puts things back. Whoever started an event, or an ADMIN, can stop it early.
- **Settings:** prices, limits, income and `adminPlays` are in the `game` block of `godeye.json` (see
  `godeye.example.json`). With `"adminPlays": false`, ADMIN acts free, without puzzles or limits.
- **The ledger:** credits live in `game.ledgerFile`, one JSON line per change, written to disk before the change
  counts. Balances, today's uses and a running event come back after a restart or a power cut.

## Security

- **The admin API**
  - It listens on loopback only.
  - The first line must carry the shared secret, or the connection is closed and logged.
  - Lines are capped at 4 KB, and at most 4 connections are allowed.
- **The web side**
  - Viewing needs no login; acting does (below).
  - It answers private LAN addresses only (`allowNetworks`).
  - WebSockets must come from the same origin, so another site open in your browser cannot read the telemetry.
  - Strict CSP, and no inline scripts or styles.
  - Static files come from a fixed list.
- **Phase 10 controls**
  - They need a login: scrypt hashes, an HttpOnly SameSite=Strict cookie, and the CSRF token on every POST.
  - Each role sees only its own actions.
  - Each action has a cooldown, and each user is capped at 20 actions a minute.
  - Every action goes to the audit log; details in [../docs/godeye.md](../docs/godeye.md).
