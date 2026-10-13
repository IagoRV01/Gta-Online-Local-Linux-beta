# Character system ("YOU")

## Model

The definition is in `shared/common/character.h`; client and server share it. A profile holds:

- **Identity**
  - `uuid`: v4, created once.
  - `name`: 2–24 characters `[A-Za-z0-9 _.-]`.
  - Model: `mp_m_freemode_01` or `mp_f_freemode_01`.
- **Heritage** (`headBlend`)
  - Mother and father faces: the indices GTA Online uses, with the creator's names (Hannah…Misty, Benjamin…John).
  - Resemblance (`shapeMix`) and skin tone (`skinMix`), from 0 = mother to 1 = father.
- **Face**: 20 micro-morphs, each -1..1 (`SET_PED_MICRO_MORPH`).
- **Hair and eyes**
  - Hair drawable (component 2), hair colour and highlight (`SET_PED_HAIR_TINT`).
  - Eye colour, 0–31.
- **Overlays**: 13 head overlays (eyebrows, beard, ageing, makeup, blush, lipstick, freckles…). Each has an index (255 = none), opacity and colours.
- **Clothes**
  - 12 components as (drawable, texture, palette).
  - 8 prop anchors (-1 = none): hat, glasses, ears, watch, bracelet.
  - Tattoos as (collection, overlay) hashes; stored and applied, but no UI yet.
- **State**: health, armour, weapons + ammo, money, position + heading, play time, deaths, inventory (reserved).
- **`revision`**: bumped on every save. In Phase 2 the server refuses saves based on an old revision.

`Validate()` clamps every field into its legal range and **rejects** profiles with a bad uuid/name, unknown schema or NaN/inf. The client runs it before applying anything to a ped; the server will run it on every upload.

## Story ⇄ YOU switch (client, Phase 1)

```
STORY (Michael/Franklin/Trevor)                         YOU (freemode)
──────────────────────────────                          ──────────────
F10 → "Play as YOU"
  checks: alive, no mission flag, no switch/cutscene
  backup story saves (once per game session)
  fade out, capture story ped (model, pos, outfit, wanted)
  [in a vehicle → warp out first]
  CREATE_PED freemode at the profile position
  CHANGE_PLAYER_PED → new ped;  story ped hidden + frozen + invincible (mission entity)
  apply look + state, stream collision, fade in
  first time → CREATE CHARACTER                ───────►  play: walk/run/drive/shoot
                                                         autosave every 15 s + on death/respawn
F10 → "Return to Story Mode"                   ◄───────
  save, fade out, CHANGE_PLAYER_PED → story ped (shown again), delete the freemode ped,
  restore its wanted level, fade in
```

What guarantees "Michael/Franklin/Trevor unchanged":
- The story ped is **never modified**. Its model, clothes, weapons and health stay on the ped object.
- The plugin **never writes** story saves or story stats.
- `SGTA5*` save files are copied to `LocalOnline\backups\saves-<date>\` before the first switch of each session.
- There is also a manual copy on the BC-250 in `~/LocalOnline-backups/`.
- Known gap: if the player **saves the story game from the phone while YOU is active**, the game writes its normal save. It records the story character's slot, not the YOU ped, but this is **untested**, so do not do it until verified. The character wheel is disabled while YOU is active.

## Creator

`F10 → Play as YOU` opens it the first time, and `F10 → Edit appearance` reopens it later.

| Section | Contents |
|---|---|
| Gender | swaps the freemode ped (resets clothes/hair to the default outfit) |
| Heritage | Mother, Father, Resemblance, Skin tone |
| Features | the 20 face sliders |
| Appearance | hair, colours, eyebrows, beard, eye colour, ageing, complexion, blemishes, freckles, sun damage, makeup, blush, lipstick |
| Clothing | top, undershirt, arms, legs, shoes, neck, mask (+ style/texture for each), hat, glasses, ears, watch, bracelet |
| Name | onscreen keyboard |
| CREATE / SAVE | validates the name and saves |

Controls:
- Arrows or D-pad to move.
- Left/right to change a value; hold to repeat with acceleration.
- Enter/A to select, Backspace/B to go back.
- Q/E or LB/RB to rotate the character.
- The camera switches between face and full body.

Counts (hairstyles, drawables, textures) are read from the ped at runtime, so DLC clothing just shows up.

## Files

| File | Where | Notes |
|---|---|---|
| `LocalOnline\cache\character.json` | BC-250 game folder | the profile; atomic writes |
| `…\character.json.bak` | same | previous save. Used automatically if the main file is corrupt. |
| `LocalOnline\backups\saves-*\` | same | story save copies |
| `LocalOnline\LocalOnline.log` | same | `timestamp LEVEL [component] [player] event` |

From Phase 2, `character.json` is a cache. The Windows database holds the authoritative copy (`server/database/schema.sql`, tables `players`, `characters`).
