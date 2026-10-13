# GodEye (Phase 9+) — design notes only

**Not being built yet**, on purpose. Work starts only when Windows Game Server ⇄ GTA clients are stable (Phase 8 done). This page pins down the boundaries now so the game server doesn't need rework later.

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

## Security baseline

- Accounts and roles: HACKER / DISPATCH / OPERATOR / ADMIN.
- Session cookies (HttpOnly, SameSite), CSRF protection on POSTs, per-user rate limits, an audit log in the `events` table.
- The admin API binds to loopback only, with a shared secret from `server.toml`.

## Open research items (before Phase 10)

- **Traffic lights:** test `SET_ENTITY_TRAFFICLIGHT_OVERRIDE` on light props found near configured intersections. If it is unreliable, build our own control layer for the enabled intersections (stop lines plus scripted vehicle behaviour).
- **Wanted:** in multiplayer, prefer a **server-owned custom wanted level** (★ alert … ★★★★★ SWAT). The server spawns and assigns police through the ownership system and mirrors it to the native wanted HUD on the owning client. GTA's native wanted level is per-instance and can't be shared consistently.
