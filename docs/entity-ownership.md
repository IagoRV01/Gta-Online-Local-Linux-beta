# Entity ownership (design, Phases 5–7)

Every networked entity has exactly **one owner client**. Only the owner simulates it and sends its
state; everyone else renders a puppet. The **server assigns and migrates ownership**, and clients only
*request* it.

```
Server ownership table
├─ player:A        → client A (permanent)
├─ player:B        → client B (permanent)
├─ vehicle:<uuid>  → driver's client; if empty, last driver; if unowned, nearest player
├─ npc:<id>        → client nearest to it (with hysteresis)
└─ world objects   → server (doors/lights state), clients only render
```

## Status

Phase 5 implements the **driver rule** only, implicitly: a vehicle is identified by its driver's
netId and lives as a puppet on the other clients. Explicit network entity ids, unoccupied vehicles,
NPCs and migration (rules 1, 3–5 below) arrive with Phase 7/8.

## Network entity ids

- The server allocates a `u32 netEntityId` for each networked entity.
- Each client maps `netEntityId ↔ local script handle`.
- Handles are never sent over the wire; they differ per GTA instance.

## Rules

1. **Create:** a client asks the server (`EntityCreate {model, pos, kind}`). The server validates (model allowed, entity count limits), allocates the id, sets the owner to the requester, and broadcasts.
2. **Driver rule:** whoever is in the driver seat owns the vehicle.
   - Entering as driver sends `OwnershipRequest`. The server grants it unless another player is already driving.
   - The previous owner hands over its last state.
3. **Proximity rule (NPCs, empty vehicles):** ownership moves to the closest player only when that player is more than 30 m closer than the current owner (hysteresis). This prevents ping-pong at boundaries.
4. **Disconnect/timeout:** all entities owned by the client are reassigned in the same tick, using rules 2 and 3.
   - Persistent vehicles with no players near are despawned on all clients and stored server-side as `state = stored/active`, with the last position.
5. **Stale owner:** if the owner sends no state for an entity for 2 s, the server reassigns it.
6. **Authority split:** the owner decides *movement*. The server decides *facts*: health in PvP, destroyed/alive, persistent vehicle state, money.

## Messages (protocol v2 additions, not implemented yet)

- `EntityCreate`, `EntityRemove`, `OwnershipRequest`, `OwnershipChanged`, `EntityState` (batched like `Snapshot`).
