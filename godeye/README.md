# GodEye

The web view of GTA Local Online. It runs on the Windows server PC next to `LocalOnlineServer.exe`.
- **Phase 9 (GodEye Basic):** a live 2D map of the players.
- Design and the later phases: [../docs/godeye.md](../docs/godeye.md), [../docs/cameras.md](../docs/cameras.md).

```
GTA clients ──ENet──► LocalOnlineServer.exe ──admin API, 127.0.0.1:4500──► GodEye backend ──WebSocket :8080──► browsers
```

| Folder | What |
|---|---|
| `backend/` | Node.js 24, TypeScript run directly (`node src/main.ts`), no npm packages. Reads the server's telemetry, adds zone/vehicle/weapon names, pushes changes to browsers, serves the web app. |
| `frontend/` | The web app: canvas map (terrain, roads, zone names, traffic lights), player markers interpolated at 60 FPS with a one-minute trail, player panel. Works on phones. |
| `data/map.json` | **Made on your PC from your own GTA files, never committed.** Without it the map is a plain grid. |
| `streaming/` | Phase 13 (Camera Client → NVENC → WebRTC), not started. |

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

`npm test`, or `node --test "test/*.test.ts"` in `backend/`, runs the unit tests.

## Security (Phase 9)

- **The admin API**
  - It listens on loopback only.
  - The first line must carry the shared secret, or the connection is closed and logged.
  - Lines are capped at 4 KB, and at most 4 connections are allowed.
- **The web side**
  - It is read-only in Phase 9.
  - It answers private LAN addresses only (`allowNetworks`).
  - WebSockets must come from the same origin, so another site open in your browser cannot read the telemetry.
  - Strict CSP, and no inline scripts or styles.
  - Static files come from a fixed list.
- **Phase 10 controls** (wanted, weather, traffic) need accounts and roles first ([../docs/godeye.md](../docs/godeye.md)).
