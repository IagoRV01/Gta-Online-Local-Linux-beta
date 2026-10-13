# GodEye — placeholder

Intentionally empty until the game server ⇄ GTA client path is stable (Phase 9).
Design notes: [../docs/godeye.md](../docs/godeye.md), [../docs/cameras.md](../docs/cameras.md).

- `backend/`: Node.js/TypeScript; WebSocket for browsers; local admin API to the game server.
- `frontend/`: 2D map first, Three.js 3D later. Own geometry only; no Rockstar assets.
- `streaming/`: Camera Client capture → NVENC → WebRTC (Phase 13).
