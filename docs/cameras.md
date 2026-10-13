# Cameras / Camera Client (Phases 12–13) — design notes only

- **Camera Client:**
  - A dedicated GTA V **Legacy** instance on the Windows PC, the same build as the BC-250 (already installed under `C:\Program Files (x86)\Steam`).
  - It runs `LocalOnline.asi` in a `camera` mode: it joins as a spectator with no ped, receives every snapshot, and renders script cameras.
  - **Never on the BC-250**, except for tests.
- **Encoder:**
  - NVENC (2× RTX 5060 Ti).
  - Capture the game window or a virtual display, encode with NVENC H.264/HEVC, and send WebRTC to the browser.
  - Candidate stack: OBS/ffmpeg + a small WebRTC gateway (e.g. mediamtx), or Sunshine's encoder path. Decide in Phase 13.
- **Camera types:** CCTV (fixed), follow, helicopter, drone, cinematic, third person, vehicle chase.
- **CCTV definition:** `{camera_id, name, position, rotation, fov, zone}`. Examples: CAM_001 Legion Square, CAM_002 Airport, CAM_003 Vinewood, CAM_004 Vespucci.
- **Control:** GodEye → game server → Camera Client (`CameraSet {mode, target netId | camera_id}`).
