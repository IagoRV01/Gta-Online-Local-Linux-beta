# Protocol v7 — GTA client ⇄ Windows game server

v2 (2026-10-08) added the vehicle block to `PlayerState`/`RemoteState`; v3 the aim point and the
combat events; v4 (2026-10-09) the wanted level (`u8`, after armour, 0..5) and `WorldState`;
v5 the economy messages; v6 personal vehicles; v7 (2026-10-09) wallet + Maze Bank accounts in
`MoneyChange`/`MoneyResult`.
Older clients are rejected with "protocol mismatch".

The source of truth is [`shared/protocol/protocol.h`](../shared/protocol/protocol.h): one header compiled into both
sides. This page explains it. Unit tests in `tests/test_main.cpp` cover round trips and hostile input.

## Transport

- **ENet** over UDP, port `game_port` (default **4499**).
- One ENet packet = one message.
- `kMaxPacket` = 64 KiB. ENet also enforces it as `maximumPacketSize`.

| Channel | Mode | Used for |
|---|---|---|
| 0 `kChanControl` | reliable, ordered | handshake, auth, profile load/save, join/leave, server commands |
| 1 `kChanState` | **unreliable, sequenced** | `PlayerState` up (20 Hz), `Snapshot` down (tick rate) |
| 2 `kChanEvents` | reliable, ordered | gameplay events: shots, damage, deaths, vehicle enter/exit (Phase 5–7) |

## Header (every message)

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | magic `0x4C` ('L') |
| 1 | u8 | protocol version (`1`) |
| 2 | u16 | `MsgType` |

All values are little endian. A wrong magic or version gets `Reject(BadVersion)` before the session exists; afterwards the packet is just dropped.

## Messages

| Type | Dir | Payload |
|---|---|---|
| 1 Hello | C→S | u32 gameBuild, str clientVersion |
| 2 Welcome | S→C | u16 tickRate, u32 serverTimeMs, str serverName |
| 3 Reject | S→C | u8 code (1 version, 2 full, 3 auth, 4 duplicate, 5 malformed, 6 banned), str reason. Then a disconnect. |
| 4 Auth | C→S | str playerName (2–24, `[A-Za-z0-9 _.-]`), str token (≤128) |
| 5 AuthResult | S→C | u8 ok, u16 netId, str playerUuid, str newToken (only on first registration), str reason |
| 6 ProfileRequest | C→S | — |
| 7 Profile | S→C | u8 exists, u32 revision, str json (≤48 KiB). `exists=0` → open the creator. |
| 8 ProfileSave | C→S | u32 baseRevision, str json |
| 9 ProfileSaved | S→C | u8 ok, u32 revision, str reason (stale `baseRevision` → ok=0) |
| 10 PlayerJoined | S→C | u16 netId, str name, str appearanceJson |
| 11 PlayerLeft | S→C | u16 netId, u8 reason |
| 12 AppearanceUpdate | C→S | str json: own look (`character::AppearanceOnly`); validated, re-serialised by the server |
| 13 PlayerAppearance | S→C | u16 netId, str json: another player's look (also sent to newcomers) |
| 20 PlayerState | C→S | see below (47 bytes total) |
| 21 Snapshot | S→C | u32 tick, u32 serverTimeMs, u8 count (≤32), count × RemoteState |

Strings are u16 length + bytes, with a per-field maximum checked on read.

### PlayerState (client's own ped, ~20 Hz)

| Field | Type | Notes |
|---|---|---|
| seq | u32 | +1 per message. The server drops anything not newer (wrap-safe `SeqNewer`). |
| clientTimeMs | u32 | for interpolation timing / latency estimates |
| pos | 3×f32 | world coordinates; NaN/inf → packet rejected |
| vel | 3×f32 | m/s, for dead reckoning |
| heading | u16 | 0..360° quantised (0.0055° steps) |
| move | u8 | 0 still, 1 walk, 2 run, 3 sprint, 4 air. Out of range → rejected. |
| flags | u16 | in vehicle, aiming, shooting, dead, ragdoll, jumping, crouch |
| health, armour | u8, u8 | health = entity health − 100 (0–100) |
| weapon | u32 | current weapon hash |

`RemoteState` (inside Snapshot) has the same fields plus `u16 netId`.

### Vehicle block (only when `flags & InVehicle`, 62 bytes)

| Field | Type | Notes |
|---|---|---|
| model | u32 | vehicle model hash (≠ 0) |
| seat | i8 | -1 driver, 0..15 passengers |
| driverNetId | u16 | the player driving (0 = NPC/empty) |
| pos | 3×f32 | vehicle entity position |
| rot | 4×f32 | quaternion (x, y, z, w); must be ~unit length |
| vel, angVel | 3×f32 each | m/s, rad/s; speed must be < 250 m/s |
| color1, color2 | u8 | primary/secondary paint |
| vflags | u8 | engine, lights, high beams, siren, horn |

The driver's client is authoritative for the vehicle transform; passengers send the block so
everyone can seat them in the right car.

### Aim point (only when `flags & (Aiming | Shooting)`, 12 bytes)

`vec3 aim`: world point under the sender's crosshair (camera ray, 80 m). Remote puppets aim there.

### Combat events (channel 2, reliable)

| Type | Dir | Payload | Server checks |
|---|---|---|---|
| 30 Shot | C→S | u32 weapon, vec3 origin, vec3 target | ≤ 25/s, shooter alive, origin ≤ 12 m from the shooter, length ≤ 1500 m; relayed as **31 ShotRelay** {u16 shooter, …} — visual only |
| 32 Damage | C→S | u16 target, u16 amount (1..300), u32 weapon, u16 bone | pvp on, both alive, distance ≤ `max_hit_distance` (400 m), ≤ 800 damage/s per attacker; forwarded to the victim as **33 DamageApply** {u16 attacker, …} |
| 34 Death | C→S | u16 killer (0 = none), u32 weapon | stored in `events` (`kill`/`death`), announced to everyone as **35 KillFeed** {u16 victim, u16 killer, u32 weapon} |

### World (Phase 7)

| Type | Dir | Payload |
|---|---|---|
| 40 WorldState | S→C | u8 hour, u8 minute, u8 second, u8 flags (1 clock frozen, 2 blackout), str weather, str nextWeather, f32 transition 0..1 |

Sent on login, every 10 s, and immediately on any change (weather cycle, admin command).

### Economy (Phase 8, channel 2)

| Type | Dir | Payload | Server rules |
|---|---|---|---|
| 50 MoneyChange | C→S | u32 requestId, u8 account, i64 delta (≠ 0), str reason (≤ 40) | account 0 wallet, 1 bank, 2 deposit (delta > 0 wallet→bank, < 0 withdraw bank→wallet); needs a loaded character; \|delta\| ≤ `max_transaction`; new money (wallet/bank > 0) ≤ `max_income_per_minute`, deposits are not income; neither account may go < 0; one ledger row per account touched (`wallet: …` / `bank: …`) |
| 51 MoneyResult | S→C | u32 requestId, u8 ok, i64 wallet, i64 bank, str reason | both balances are authoritative; requestId 0 = server-initiated (fees: taken from the bank, else the wallet) |

Profile saves never change money: the `characters.money` column is the balance. A new or migrated
character may bring at most `starting_money_cap`.

### Personal vehicles (Phase 8)

| Type | Dir | Payload |
|---|---|---|
| 60 VehicleListRequest | C→S | — |
| 61 VehicleList | S→C | u8 count (≤ 32), count × str json (`vehicle::ToJson`: uuid, model, plate, colours, 50 mods, turbo/xenon, health, position, state) |
| 62 VehicleRegister | C→S | u32 requestId, str json — the car you drive becomes yours (state `active`); ≤ 10 per character; plate `[A-Z0-9 ]{1,8}` |
| 63 VehicleUpdate | C→S | u32 requestId, str uuid, u8 state, vec3 pos, f32 heading, f32 engine, f32 body |
| 64 VehicleResult | S→C | u32 requestId, u8 ok, str uuid, str reason |

States and allowed transitions (`vehicle::CanTransition`): stored → active (call out); active →
active/stored/destroyed/impounded; destroyed → stored (insurance, `insurance_cost`, repaired);
impounded → stored (`impound_cost`). Only the owner's character can change a vehicle.

`amount` is computed by the shooter with `proto::PvpDamage(weapon base damage, bone, measured)`:
base × 2.5 for head/neck, × 0.8 for arms/legs, × 1 otherwise; the health a puppet loses locally is
only used to detect *that* a hit happened (story mode inflates player-vs-ped damage: a pistol round
measured 208 on a ped).

## Safety rules (server side)

- Size cap before parsing. **Every** read is bounds-checked (`wire::Reader`). A truncated packet, trailing garbage, non-finite floats or bad enums reject the **whole** message. Nothing is ever partially applied.
- A per-peer rate limit applies (`max_packets_per_second`, default 120). Excess packets are dropped and counted. More than 500 malformed packets disconnects the peer.
- `PlayerState` before `Auth` is ignored. The same name can't log in twice. A server-full reject still lets the client receive the reason.
- Replay/staleness: unreliable state uses `seq` (older = dropped). Reliable writes (profile) use `revision` (stale = refused).
- Peer timeout: ENet `timeout_ms` (10 s).
- Auth (Phase 2, implemented): on the first login of a new name (`[accounts] allow_registration = true`) the server creates the account and returns a random 24-byte token **once**; the client stores it in `LocalOnline\cache\token`, the server only keeps its SHA-256. Later logins must present it ("wrong token" otherwise). Turn registration off once everyone has an account. LAN-grade protection (no transport encryption); the open Internet would need DTLS/QUIC.
- Profile saves: at most one every 500 ms per client; the JSON is parsed and validated with the same `character::FromJson/Validate` as the client; one character per account; stale `baseRevision` → `ok=0`.

## Rates and bandwidth (LAN, 2–4 players)

- Up: 47 B × 20 Hz ≈ 1 KB/s per client, plus ENet overhead (about 2 KB/s).
- Down: 11 B header + 45 B per other player × 20 Hz. With 4 players that is about 3 KB/s.
- Snapshots are full state, not deltas. Delta compression is unnecessary at this size; revisit above 16 players.

## Evolution rules

- Adding a message type is backwards compatible (unknown types are ignored as malformed-but-harmless).
- Changing a payload layout **bumps `kProtocolVersion`**. Old clients get a clear "protocol mismatch" reject.
