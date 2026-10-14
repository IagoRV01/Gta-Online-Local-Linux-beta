# Multiplayer design (Phases 4–7)

The target is 2–4 players on a LAN. Sync quality matters more than player count.

## Model: client-owned avatars, server-authoritative everything else

- Each client **simulates its own ped** locally, so input has zero latency, and streams `PlayerState` at 20 Hz.
- The server **validates** each state: rate, sequence, plausible speed and position. It keeps the latest state and broadcasts `Snapshot`s at the tick rate.
- Remote players are **puppet peds**, created and driven by the receiving client:
  - **Interpolation buffer**: render remote players about 100 ms in the past (2 snapshots at 20 Hz). Lerp the position and slerp the heading between the two snapshots around render time.
  - **Limited extrapolation**: when the buffer runs dry (loss), dead-reckon from the last velocity for at most 250 ms, then freeze. Snap if the error is over 3 m; otherwise blend over 150 ms.
  - Locomotion is driven by `move` (still/walk/run/sprint) through `TASK_GO_STRAIGHT_TO_COORD` / `SET_PED_DESIRED_MOVE_BLEND_RATIO`, not by teleporting every frame. Warping a ped every frame breaks its animations; an earlier project confirmed this.
- **No client-side prediction or reconciliation** for the own ped. The server does not simulate GTA physics, so there is nothing to reconcile against. The server *may* correct (teleport) a client that is clearly invalid: speed over 150 m/s on foot, or out of the map.

## Phase order (each one "perfect" before the next)

| Phase | Synced | Channel |
|---|---|---|
| 4 | position, heading, velocity, move state | state |
| 5 | vehicle model/colour on spawn, enter/exit (event), vehicle position/rotation/velocity/steering from the **owner**, passengers | events + state |
| 6 | weapon in hand, aim, shots (origin, direction, weapon) as events; damage is **reported by the shooter's client and decided by the server** (health is server-owned in PvP); death/respawn as server events | events |
| 7 | clock + weather (server-owned, pushed every 10 s), wanted (see GodEye doc), NPC/police (ownership, see entity-ownership.md) | control/events |

## Phase 5 as implemented (2026-10-08)

- **Ownership:** the driver owns the vehicle; only the driver's client moves it. A remote driver
  becomes a local *puppet vehicle* (same model and colours, plate = player name, doors locked,
  invincible until damage sync exists).
- **Driving the puppet:** each frame `velocity = owner velocity + 3 × position error` (the
  position error is measured against the interpolated sample), orientation = interpolated quaternion
  (nlerp), snap when the error exceeds 6 m, front wheels steered from the yaw rate (cosmetic),
  engine/lights/siren/horn applied from the flags.
- **Enter/exit:** entering is an instant seat (the owner already did the animation); leaving plays
  `TASK_LEAVE_VEHICLE` (warp after 3 s). A puppet car left behind stays parked locally and is removed
  after 2 minutes unused. Passengers are seated in the driver's puppet car, or in our own car when we
  are the driver.
- **Measured:** 0.04–0.36 m error at 15 m/s on a 30 m circle (netprobe → BC-250).
- **Not yet:** vehicle damage, unoccupied-vehicle ownership migration (who simulates a car nobody
  drives), persistent personal vehicles (Phase 8), passengers entering a remote player's car locally.

## Phase 6 as implemented (2026-10-08)

- **Weapons:** the weapon in hand travels in every state; puppets are given that weapon (infinite
  clip) and aim at the sender's aim point (`TASK_AIM_GUN_AT_COORD`, or
  `TASK_GO_TO_COORD_WHILE_AIMING_AT_COORD` while moving).
- **Shots:** the shooter's client sends Shot events (≤ 15/s) with muzzle → impact; others draw them
  with `SHOOT_SINGLE_BULLET_BETWEEN_COORDS_IGNORE_ENTITY` and **damage 0** (tracer, flash, sound,
  impact) owned by the shooter's puppet.
- **Damage:** puppets have 1000 hp and never die from local hits. When a puppet loses health and
  `HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(puppet, our ped or our car)` is true, the client sends Damage
  (PvP amount from the weapon's base damage and the bone), restores the puppet's health and clears
  the damage record. The server validates and forwards it; the victim applies it with
  `APPLY_DAMAGE_TO_PED` (armour first).
- **Death/respawn:** the victim's own death handling (ADR-006) respawns it; it reports Death with
  the last attacker of the previous 10 s; everyone shows the kill feed ("X killed Y"). Puppets die
  when the sender's state says dead and are resurrected when it is alive again.
- **Measured (netprobe ⇄ BC-250, 2026-10-08):** the probe's hits (30 each) took Gori 200 → 170 →
  140 → 111 → dead; kill feed on both ends; Gori respawned as Gori. Gori's pistol hit on the probe
  was detected and reported (before the damage-model fix it measured 208 and one-shot the probe).
- **Not yet:** server-side health authority (the victim is trusted to apply the damage), vehicle
  damage, explosions' area damage to other players, NPC kills credited across clients.

## Phase 7 as implemented (2026-10-09)

- **Server-owned world** (`shared/common/world.*`, unit-tested): a clock at GTA's own rate
  (2 s per game minute; can be frozen) and a weather cycle (plausible successions, 15–30 min holds,
  45 s blends). Persisted in `world_state` (`clock_minutes`, `clock_frozen`, `weather`,
  `blackout`) so it survives server restarts.
- **Clients** apply it only while YOU is online: `SET_CLOCK_TIME` when drift > 2 game minutes,
  `PAUSE_CLOCK` when frozen, `SET_WEATHER_TYPE_NOW/OVERTIME_PERSIST`, `SET_ARTIFICIAL_LIGHTS_STATE`
  for the blackout. The story clock and weather are captured on entering YOU and restored on
  leaving, so story mode keeps its own time.
- **Wanted:** each player's GTA wanted level travels in its state; others see it as stars on the
  name tag and a flashing red blip. Police stay local to each client (each GTA chases its own
  player); a server-owned wanted system is the GodEye phase.
- **Admin:** `D:\GTALocalOnline\server.cmd` — `time H M`, `freeze on|off`, `weather NAME|auto`,
  `blackout on|off`; validated, logged, stored in `events`, pushed at once. Verified with netprobe:
  `time 22 30`, `weather rain`, `blackout on` arrived immediately, `weather LAVA` rejected.
- **Not yet:** NPC/traffic sync (each client has its own ambient world), shared police.

## Phase 8 as implemented (2026-10-09)

- **Economy:** money only changes through `MoneyChange` (server-validated, ledger). The client
  applies a change optimistically (purchases feel instant) and adopts the server balance from the
  answer; offline changes are queued in `cache\economy_pending.json` and replayed on reconnect
  (unconfirmed in-flight ones are dropped to avoid double charges). Gori Online's purchases use it
  through `LocalOnline_AddMoney`.
- **Personal vehicles (F10 → My vehicles):** "Make this vehicle mine" registers the car you drive
  (paint, 50 mod slots, turbo/xenon, tint, plate, livery, health); call a stored car to the nearest
  road; store it; destroyed cars are claimed from the insurance. While driving one, its position
  and health are saved every 30 s; leaving Local Online stores every called-out car. Blip sprite
  225 ("personal vehicle").

## Maze Bank and the Online HUD (2026-10-09)

- **Two accounts, like GTA Online:** wallet (cash on hand) and Maze Bank. Schema v2 adds
  `characters.bank` (the server migrates a v1 database by itself on start; back up first). Earnings
  that come through `LocalOnline_AddMoney` (Gori Online jobs, sales, heists) go to the bank;
  costs take the wallet first, then the bank. `LocalOnline_Spend(amount, reason, preferBank)` pays
  internet/phone purchases from the bank. Exports API v2 adds `GetWallet`, `GetBank`, `Spend`,
  `ChangeAccount(0 wallet | 1 bank | 2 deposit, delta, reason)`.
- **ATMs:** every `prop_atm_01/02/03` and `prop_fleeca_atm` — "Press E to use the ATM" → Maze Bank
  menu (deposit all, deposit amount, withdraw $100…$100,000 or a typed amount) with the ATM
  animation. **F10 → Maze Bank** is online banking: deposits only, withdrawals need an ATM.
- **HUD:** the story cash HUD is hidden while YOU plays. Top right, Pricedown font: BANK and CASH
  balances, shown for 6 s after any change (with green `+$` / red `-$` change lines) and while
  **Z** (INPUT_MULTIPLAYER_INFO) is held. Holding Z also shows the player list (you highlighted,
  wanted stars). Other players get the game's own Online gamer tag (`CREATE_FAKE_MP_GAMER_TAG`,
  name within 100 m, wanted stars) and a white blip with heading arrow in the "Other Players"
  legend category; the drawn name tag remains as a fallback if the game refuses a tag.
- **Test commands:** `bank.status`, `bank.deposit N`, `bank.withdraw N`, `bank.menu`, `bank.atm`.
- **Rollback:** a v1 server refuses a v2 database. Restore the pre-migration backup from
  `D:\GTALocalOnline\backups\` together with the previous server and plugin builds.

## Gori Online per character (2026-10-09)

- Character JSON gains `mods`: other mods' per-character state (keys `[a-z0-9_.-]{1,32}`, at most 8,
  24 KB each), saved with the profile on the server and in the cache, never relayed to other
  players. Exports API v3: `LocalOnline_GetData(key, buf, cap)` / `LocalOnline_SetData(key, value)`;
  a change is uploaded within ~2 s.
- Gori Online (gta-mods/GoriOnlineNative, not in this repo) keeps two profiles: the story
  characters share `GoriOnline.ini`; YOU has its own under the key `gorionline` (properties,
  interiors and their upgrades, businesses, heist progress, RP/rank, casino chips, its garage
  cars). It switches when Local Online enters or leaves YOU: running missions end, garage cars are
  removed, blips redrawn. A new YOU profile starts like a new Online player (starter bonus to the
  bank, Isla Neon villa).
- Payments: internet purchases (cars, properties, upgrades) are paid from the Maze Bank account;
  other purchases take cash first, then the bank. The internet header shows the bank balance.
- Rollback: `GoriOnline.asi.bak-v7-before-profiles` and `GoriOnline.ini.bak-v7-before-profiles` in
  the game folder; source backups `*.v7-before-profiles.*.bak` next to the source.

## GTA Online properties in Gori Online (2026-10-10)

All of GTA Online's apartments, houses, garages, offices and MC clubhouses (101 properties, 83
buildings) come from the game's own data: `gta-mods/GoriOnlineNative/tools/gen_online_props.py`
reads the decompiled GTA Online scripts (acidlabsdev/gtav-legacy-scripts: `startup.c`, which fills
the property table at boot, and `am_mp_property_ext.c`, the exterior cutscenes) and writes
`online_props.h`.

- **Entrances:** walk-in and garage markers are computed exactly like GTA Online (door + door
  template, `func_58`), drawn like `am_mp_property_ext` (blue, 0.75 m, 0.25 m under the ground).
  Driving into the garage uses Online's trigger area. One marker per building; several owned
  units in one building are picked with left/right. Verified: 138/143 markers on the ground (the
  other 5 are the offices' helipad entrances, which Online also places in the air, and one
  clubhouse garage).
- **Inside:** the entry spot and the exit area at the apartment's front door are GTA Online's
  interior template points, read at run time with `GET_BASE_ELEMENT_LOCATION_FROM_METADATA_BLOCK`
  (it works in story mode) and moved to each property like `func_48`. You leave by walking to the
  front door: "exit to the street" or "go to the garage". 74/74 interiors load (2866 Hillcrest
  needs `apa_stilt_ch2_09c_int`, loaded on demand like Online).
- **Cutscenes:** walking in (Online's camera and route, all 56 buildings with a door), walking out
  (lobby/building shot), driving into the garage and walking into it (Online's scenes where the
  game has them, the building's garage camera otherwise), leaving the garage by car or on foot
  (the same routes backwards). Gori Online's own properties (bunkers, nightclubs...) get a
  generic shot.
- **Exits:** on foot at Online's street spot, by car at Online's vehicle exit.
- Prices: gtaboss.gg for high-end apartments, houses, penthouses and clubhouses; medium/low-end
  apartments and garages inside the official tier ranges.
- Test commands (GoriOnline.cmd): `props`, `testents`, `testint`, `buyprop`/`delprop`,
  `entrance [garage] <name>`, `enter`, `door`, `leave`, `garage`, `garexit`, `park`, `driveout`,
  `meta <map> <from> [to]`.

## Online interface (2026-10-10)

- **Rank bar (Gori Online):** GTA Online's own `MP_RANK_BAR` (script HUD movie 19), driven like
  freemode.ysc: `SET_COLOUR(116, 123)`, `OVERRIDE_ONSCREEN_DURATION`, `SET_RANK_SCORES(rank start,
  next rank, from, to, rank, 100)`, one call per rank crossed with `OVERRIDE_ANIMATION_SPEED(2000)`.
  Shown while Z / d-pad down is held and whenever RP is earned. Ranks use GTA Online's RP table
  (freemode `func_2211`: 800, 2100, 3800 ... 1,555,800 at 99, then +28,500 +50/rank). The drawn
  bar is only a fallback if the movie cannot load.
- **Pause menu (LocalOnline, while YOU):** ESC / P / Start are taken by the plugin, which opens the
  same SP pause menu itself (`ACTIVATE_FRONTEND_MENU`): when the game opens it on its own, scripts
  stop and the header can't be changed (it showed MICHAEL again after a death). Opened this way the
  world keeps running, as in GTA Online.
- **Pause menu header (LocalOnline, while YOU):** "Grand Theft Auto Online", the player name, day
  and time, "BANK $x CASH $y" (frontend header `SET_HEADER_TITLE` / `SET_HEADING_DETAILS`) instead of
  the story character's name and money.
- **Story blips hidden while YOU** (restored on the way back to story mode): Strangers and Freaks
  (sprite 66), story businesses (374, 375, 388, 389), base jumps (94), story races (314-316), car
  wash (100) and the story characters' parked cars (225 without our colour). Shops, Ammu-Nation,
  LSC, barbers, tattoos, golf, darts, strip club, casino and the mods' own blips stay.
- Test commands: LocalOnline `blips` (dump), `ui.hidden`, `ui.pause`, `ui.close`; Gori Online
  `rank`, `giverp N`.

## The game's own internet (Gori Online, in progress since 2026-10-10)

The phone's browser now opens the game's own `web_browser` scaleform movie, driven like GTA
Online's `appinternet.ysc` (`gta-mods/GoriOnlineNative/Web.inc`): GO_TO_WEBPAGE, wait for the
site movie, INITIALISE_WEBSITE, then the site's data, mouse/pad input exactly as appinternet sends
it (SET_MOUSE_INPUT, SET_ANALOG_STICK_INPUT, SET_INPUT_EVENT 16/4-9, GET_CURRENT_SELECTION).
- Website ids (GET_CURRENT_WEBSITE_ID): eyefind 2, Legendary 10, Elitas 11, Warstock 12,
  Docktease 13, P&M 15, Southern SA 16, Dynasty 8 18, Maze Bank 21, Benny's 26, Foreclosures 28,
  Arena War 29. The id lags a few frames after INITIALISE_WEBSITE.
- Vehicle sites: each vehicle has its own page (page id = slot); SET_PRICES(slot, price) before
  the list page draws shows GTA Online's catalogue. ORDER (selection 0 on a vehicle page) ->
  "Purchase pending" page -> we buy (Maze Bank, garage with room, chosen colour) ->
  `<SITE>_S_PURCHASE_D_SUCCESS` / `_ERROR`.
- Full catalogue (2026-10-10): `tools/gen_web_vehicles.py` reads every slot GTA Online sells.
  - It reads func_8744 (sold in Online) and func_8804, including its `switch (num - k)` blocks.
  - It also reads raw-hash, Global and func models from func_8796.
  - Result: 728 pages. Legendary 210, Southern SA 266, Warstock 145, Elitas 32, Docktease 19,
    P&M 8, Benny's 26, Arena 22.
  - Build 3889 has every vehicle. In game, `webcat` reports 0 hidden except 3 special purchases:
    the yacht, the MOC and the ballistic equipment. They wait for the special vehicles phase.
- Prices: GTA Online takes them from Rockstar's catalog server (NET_GAMESERVER_GET_PRICE).
  - `tools/fetch_wiki_prices.py` builds `tools/web_prices.json` from the GTA wiki's "Stock in GTA
    Online" pages, plus a manual list.
  - Three unreleased cars use estimates: Horus, Cartuccia GT, Ignus Pursuit.
  - Gori Online's own catalogue uses the same prices.
- eyefind: weather, zone, day, news (NWS_PRO_HLn/Sn, after REQUEST_ADDITIONAL_TEXT("NWS_PRO", 4)),
  bleets (BLE_n), sponsored blocks.
- The browser must get `SET_BROWSER_SKIN(1)` and `SET_MULTIPLAYER(true)` when it opens.
  - Skins: 0 Michael, 1 Online, 2 Franklin, 3 Trevor.
  - Without skin 1, the sites show their story pages: Dynasty 8 listings go to "maintenance",
    Benny's is "under construction", and Maze Bank greets Michael.
  - The sites' ActionScript is decompiled at floppystudiodev/GTAV-Scaleforms-Decompiled.
  - There, PAGE_NAMES gives the GO_TO_WEBPAGE names: the URL path, with "-" written as "_D_".
- Dynasty 8 (site 18) works like appinternet func_9107/func_9108.
  - Listing: 85 properties (ids 1-85 not owned) with GTA Online's default prices and labels.
  - Picking one in the listing, then page 21 -> 4 (PURCHASE), buys it from the bank.
  - The site then shows PURCHASE_D_SUCCESS, or PURCHASE_D_ERROR with the reason.
  - Page 21 -> 5 (GETDIRECTIONS) sets a waypoint.
- Maze Bank (site 21) works like appinternet func_9000.
  - Home, menu, withdraw/deposit amounts (func_9005), confirm, done, error.
  - Money moves through the LocalOnline export ChangeAccount.
  - The transaction log is the server ledger: protocol v8 LedgerRequest/Ledger, the newest 16 rows,
    exports API v4 RequestLedger/GetLedger.
- Benny's (site 26) shows its Online site with skin 1.
- Dynasty 8 Executive (site 27) and Maze Bank Foreclosures (site 28) are "dynamic" sites.
  - Rows go out as SET_DATA_SLOT + APPEND_OFFICE_DATA_SLOT in fixed slots (owned property, then
    the rows). The choices come back packed in GameGlobals flags: OFFICE 12, CLUBHOUSE 13,
    GARAGE_1/2 14/15. The page after the summary is PURCHASE.
  - Executive: 4 offices with 9 styles and the add-ons, and the office garage (3 levels).
  - Foreclosures: 58 rows (12 clubhouses, 11 bunkers, 5 hangars, 9 facilities, 10 nightclubs,
    6 arcades, 5 auto shops). Gori Online lacked 3 hangars, 4 arcades and 3 auto shops; they are
    added at GTA Online's spot with the type's interior.
  - `tools/gen_foreclosures.py` writes `foreclosures.h` by running appinternet's own feed
    functions in `tools/ysc_eval.py`. That is a small interpreter for the decompiled scripts:
    switch, if/else, tunable defaults and a scaleform-call recorder.
  - The cost of a choice follows the site's own classes (setupCosts: options with a leading 0
    cost nothing when "none").
  - The result page needs its heading and text in slots 1/2 before the GO_TO: MP_OFF_S_BUY_T /
    MP_OFF_F_BUY_T and MP_<CLU|BUN|HAN|DBA|NCL|ARC|AUT|OFF|GAR>_S_BUY / _S_REN / _F_BUY_2.
- `client/linux/restart-online.sh` restarts GTA, gets past Rockstar's landing page, enters YOU.

## The GTA Online phone (Gori Online `Phone.inc`, 2026-10-11)

While YOU plays, the story phone becomes GTA Online's. Verified in game.
- **App table.** The story phone's app table is `Global_9509` (elements at 9510 + i*15).
  - Fields: label, position, script, hash, icon and page.
  - Gori writes GTA Online's 9 apps into it every frame: Email, Texts, Contacts, Quick Join, Job List, Settings, Snapmatic, Internet and SecuroServ.
  - It restores the story table on exit.
- **Home screen redraw.** The controller draws the home screen from its own table as the phone opens. Gori then redraws it:
  - SET_DATA_SLOT(1, pos, icon, 0, label) on the phone movie `Global_21630`, then DISPLAY_VIEW(1, `Global_21629`);
  - it points the position → app map (`Global_10050`) and the drawn flags (`Global_10087`) at the Online apps.
- **Contacts.** The character sheet is `Global_2349` (elements 2350 + i*29). The owner is `Global_21649` (3 = multiplayer).
  - Gori sets two per-owner flags for 15 Online contacts: f_12 (in the phone book) and f_24 (name known).
  - Without f_24, appContacts lists only the bare number.
  - The contacts: Lester, Simeon, Lamar, Ron, Martin, Gerald, Agent 14, Brucie, Mechanic, Pegasus, Mors Mutual, Merryweather, Assistant, Tony and Ms. Baker.
  - Pictures: the charsheet's f_7 is a text label (CELL_311 → CHAR_LESTER), and Gori streams that dictionary. Still open (2026-10-12): Lester stays a silhouette even with CHAR_LESTER loaded, while the other contacts with the same charsheet flags show their picture.
  - Since the 2026-10-11 fix (the home-screen redraw only runs below state 6), Contacts opens again (verified 2026-10-12).
- **Calls.** A call lasts 1.8 s while the phone state `Global_21649.f_1` is 10 (calling `Global_8817`). Then Gori:
  1. closes appContacts;
  2. shows the contact's line;
  3. opens the matching Gori Online menu (Heists, Vehicles, Services, Businesses).
- **App intercepts.** Job List and Quick Join open Heists; SecuroServ opens Businesses.
- **Putting the phone away.** It must set the phone state back to 3, like cellphone_controller's func_98.
  - If the state stays at 6 or 10, the controller keeps the phone "up": it swallows the back button and the phone never opens again.
- **Test commands:** `phonescan [from to]`, `phonechars`, `globals <from> <count>`.

## Special vehicles (Gori Online `SpecialVehicles.inc`, 2026-10-11)

The MOC, Avenger and Terrorbyte (and the Kosatka's options) are bought on Warstock's own configurators, then requested and walked into. Purchase and the MOC's interior were verified in game, and every interior and station was checked.
- **Configurators.** They are the site's own pages: TRUCK (MOC, pages 54-62), AVENGER (83-90, 163), HACKERTRUCK (101-110) and SUB (138-147).
  - `tools/gen_warstock_specials.py` builds `warstock_specials.h` by running appinternet's feeds (func_8830/8824/8819/8812) in `tools/ysc_eval.py`.
  - Those feeds fill slots 10-31: the owned configuration first, then prices.
  - The site packs your choices into GameGlobals TRUCK (17) or CHOPPER (18) and goes to its PURCHASE page. Gori charges the site's own total (its `ProgressPanel.updateCosts`).
  - A missing Bunker, Facility or Nightclub goes to the failed page with GTA Online's message.
  - Ownership is stored as `g_cust["@MOC|config"]` etc., holding the flag value.
  - Example: the MOC with Command Center and Weapon & Vehicle Workshop totals $2,500,000.
- **Interiors.**
  - Avenger: `xm_x17dlc_int_01` at (520, 4750, -70). Entity sets: SHELL_TINT, CONTROL_1-3, WEAPONS_MOD, VEHICLE_MOD, touch_screen.
  - Terrorbyte: `ba_dlc_int_03_ba` at (-1421, -3012.6, -80). Entity sets: Int_03_ba_Design_NN, Int_03_ba_Tint, drone, weapons_mod, bikemod.
  - MOC: `gr_grdlc_int_01` at (1103.56, -3000, -40).
    - Its three bays are invisible `gr_prop_inttruck_anchor` buildings. They are model-swapped (`CREATE_MODEL_SWAP`) with `gr_prop_inttruck_<command|gunmod|carmod|living|vehicle|empty>_01`, like am_mp_armory_truck func_7313.
    - The colour is `SET_TINT_INDEX_CLOSEST_BUILDING_OF_TYPE` with tint = colour + 1.
  - Tint 0 on the MOC sections or `Int_03_ba_Tint` is a debug palette (green, purple, cyan).
- **In the world.** Services lists Request / Return for each vehicle. The MOC spawns as a Phantom or Hauler Custom with `trailerlarge` attached.
  - Press E behind the vehicle (the model's rear plus 1.2 m) to enter.
  - Inside are stations: exit, computer (Command Center / operations / Nerve Center → Businesses or Heists), Weapon Workshop and Vehicle Workshop.
- **Weapon Workshop.**
  - Upgrades to Mk II at the DLC weapon price.
  - Tints: the 8 base tints at GTA Online's prices; the 32 Mk II tints at $5,000 each.
  - Attachments: for base weapons, `weapon_mods.h` comes from gunclub_shop func_166's multiplayer branch (`tools/gen_weapon_mods.py`). For DLC weapons, prices come from `GET_DLC_WEAPON_COMPONENT_DATA` at run time.
  - Purchases are kept in `g_cust` (`@wt|`, `@wc|`) and reapplied every 2 s, because LocalOnline only saves weapon hash and ammo.
- **Vehicle Workshop.** Your garage vehicle is spawned in the bay (MOC rear half, Avenger hold), frozen on the floor, with you in it.
  - The menu covers GTA Online's mod slots, Turbo, Xenon lights, window tint and respray. "Done" saves the vehicle into its garage record.
  - With the Terrorbyte, the vehicle is brought next to the truck instead.
- **ysc_eval fixes.** Case labels of nested switches are no longer matched, and `*Global_262145.f_N` is read. The first bug had given Foreclosures wrong add-on prices, so `foreclosures.h` was regenerated.
- **Test commands:** `spvcfg <key> <flag>`, `spvin <key>`, `spvout`, `spvreq <key>`, `delspv <key>` (keys: MOC, Avenger, Terrorbyte, Kosatka), and `tpx x y z h` (no ground snap).

### Turrets, autopilot and drone (2026-10-11, verified in game 2026-10-12)

These are used from inside the vehicles. GTA Online runs them as `turret_cam_script` / `am_mp_drone`; Gori does the same with its own scripted camera, which the streaming focus follows. The weapon is fired with `SHOOT_SINGLE_BULLET_BETWEEN_COORDS_IGNORE_ENTITY`.
- **Avenger turrets.** The turret seats in the hold (if bought) give a camera at `turret_1/2/3base` (nose, top, tail) that fires `VEHICLE_WEAPON_AVENGER_CANNON`. LB/RB or the mouse wheel changes turret.
- **Avenger autopilot.** Services → Avenger: Autopilot while flying. A hidden pilot keeps circling (`TASK_PLANE_MISSION` type 9) and you go to the hold.
  - The cockpit station takes the controls back.
  - Leaving while it flies drops you behind it with a parachute. It sets the coordinates directly; `Teleport()` snapped to the ground and killed you (fixed 2026-10-12).
  - Test commands: `avtest` (puts the Avenger 350 m up with you at the controls, then engages the autopilot), `avstat`, `avctl` (takes the controls back), `spvout`.
- **Terrorbyte missiles.** The Turret Station fires `VEHICLE_WEAPON_HACKER_MISSILE` from the roof battery (`weapon_1a/1b`, alternating).
- **Terrorbyte drone.** The Drone Station flies `ba_prop_battle_drone_quad` from the truck.
  - Controls: mouse to look, WASD, jump/duck for up/down, sprint to boost.
  - The stun is a `WEAPON_STUNGUN` shot.
  - It loses the signal past 500 m and ends at 600 m.
- **Probe commands:** `seats <model>` (turret seats: neither vehicle has any), `probe <model> <bone|weapon>...`, `turret [Terrorbyte]`, `drone`, `remoteoff`.

### Ballistic Equipment (2026-10-11)

- **Buying.** Warstock (catalogue 324) sells it for $500,000. Gori asks for a Bunker; GTA Online checks a research unlock instead.
- **Requesting.** Services → Request Ballistic Equipment drops a Merryweather crate nearby (5 min cooldown). Press E at the crate to put the suit on.
- **The suit.** It is freemode's own (func_19799/19791/19792):
  - mask slot `DLC_MP_GR_<M|F>_BERD_1_25`. Story mode has no shop data, so the drawable is found by `GET_HASH_NAME_FOR_COMPONENT`.
  - head prop 39 (male) / 38 (female);
  - 2600 health, no critical hits, no cover, no vehicles;
  - the Minigun (tint 7) and the ballistic clipsets.
- **Taking it off.** Remove (or dying) gives the outfit, health and weapons back. A Minigun that only came with the suit is removed again, even after a reload.

### Galaxy Super Yacht (2026-10-11)

- **Docktease.** Its yacht configurator is fed with appinternet func_8838's data (`warstock_specials.h`, feeds `yacht` / `yacht_owned`: SET_DATA_SLOT 18 plus APPEND_YACHT_DATA_SLOT rows).
  - The name box (selection 0 on YACHT_D_PERSONALIZE) opens the on-screen keyboard. The name goes back in DATA_NEW_NAME, which the site polls.
  - The purchase reads GameGlobals YACHT (11): colour bits 6-11, lighting 12-17, flag 18-23, model 24-25, fittings 26-27.
  - It charges each changed part (getTotalPrice): model $6M/7M/8M, fittings, lights, colour, flag, and a name change once owned.
- **Moorings.** `tools/gen_yachts.py` builds `yachts.h`: the 36 moorings `apa_yacht_grpNN_M` (+ `_int`), from DurtyFree's ipls.json / mloInteriors.json, and the 46 flags (freemode func_1815).
- **Building the yacht.** Within 500 m, the ymap hull loads and Gori adds freemode's parts:
  - decks `apa_mp_apa_yacht_optionN[_colX]` (tint = colour), lights `apa_mp_apa_yN_l..`, rails, 3 radars, jacuzzi, flag;
  - at the ymap centre, z = 20, heading = the interior's heading (`GET_INTERIOR_HEADING` is in radians);
  - the hull object `apa_mp_apa_yacht` is tinted once it has streamed in.
- **Services.** Travel to your Yacht (inside its interior), and Move your Yacht (12 places).
- **Test commands:** `yachtcfg <flag> [spot]`, `yachtgo`, `delyacht`, `inthead x y z`, `objat <model> x y z h` / `objclear`.
- **Server gotcha:** `max_income_per_minute = 5000000` also caps the test `money` command.

## LS Customs and Ammu-Nation for YOU (Gori `Shops.inc`, 2026-10-12, verified in game)

The story shop scripts (carmod_shop / gunclub_shop) are written for Michael, Franklin and Trevor, and do not work with the freemode character. While YOU plays, Gori stops them and runs GTA Online's versions instead.
- **LS Customs.** Bring your personal vehicle to any of the 6 garages (Burton, La Mesa, LSIA, Route 68, Paleto Bay, Benny's) and press E at the door.
  - The car is frozen there and the Vehicle Workshop menu opens: every mod slot, respray, window tint, plus Repair.
  - Mod prices are Online's, based on the catalogue value. Modifications are saved on the garage record.
  - Other cars: the existing "sell to LS Customs" prompt.
- **Ammu-Nation.** Press E at any of the 11 counters to open the menu.
  - Weapons by category with Online prices; once owned, buy ammo (two magazines a time).
  - Armor ($500 to $2,500), Parachute, and the Weapon Workshop (tints, attachments, Mk II).
  - Prices: `ammunation.h`, generated by `tools/gen_ammunation.py` from gunclub_shop's multiplayer prices (func_219) with the tunable defaults.
- **Payment.** Cash first, then the bank, like Online.
- **Test commands:** `ammu` (opens the menu anywhere), `lsc` (LS Customs on the current car), `shops` (logs what is near).

## Races (protocol v10, Gori `Races.inc`, 2026-10-12)

GTA Online-style races, solo or online, with GTA Online's look: countdown, checkpoints, HUD, results.
- **Tracks.** Community Races XML files in `GoriOnline\Races` (next to GTA5.exe):
  - 41 community races from github.com/oldnapalm/CommunityRaces;
  - alloc8or's 63 official GTA Online land races (gta5-mods "GTA Online Races [Community Races]").
  - Each race lists checkpoints, spawn points, available vehicles, laps, decorative props (spawned for the race) and its trigger.
- **On the map.** Every race has a race-flag blip at its start; press E there or use Interaction Menu → Races.
  - Pick the laps and the vehicle, then Race Solo or Host Online Race.
  - Other players join the lobby from Jobs. The host starts it from Races → Start Race.
- **Racing.**
  - The server sets one start time (+6 s) for everyone, and each player is put on their own grid slot (host 0, members in join order).
  - Countdown with the COUNTDOWN scaleform, chevron checkpoints (rings for aircraft) plus current/next blips with GPS, and a timer-bar HUD (time, lap, checkpoint, position).
  - Hold F / Y to respawn at the last checkpoint.
  - Results board when it ends; the host ends it when everyone has finished or 60 s after its own finish.
- **Protocol v10.**
  - Jobs gain a `kind` (0 mission, 1 race).
  - JobState carries `startAt` (server ms). The client turns it into "starts in" with the snapshot clock.
  - RaceProgress (75, C): checkpoints and finish time. RaceStandings (76, S): the race order.
  - A race can be joined until its countdown ends.
- **Payout.** The host sets it: 1500 + $100 per checkpoint, max $30,000. The server pays every racer by position (100/75/60/50/40/35/30/25 %); DNF gets 10 % and nothing is paid if the racer never left the grid. RP comes from Gori.
- **Race Creator.** It turns the props-only stunt tracks into races: Map Editor/Menyoo files in `GoriOnline\Tracks`, including alloc8or's 129 GTA Online stunt maps.
  - Races → Race Creator loads a track at its TeleportPoint; E drops a checkpoint.
  - Set Start Grid builds 8 places behind the car. Save writes `GoriOnline\Races\<track>.xml` with the props, and the current vehicle becomes the race vehicle.
- **Test commands.**
  - Gori: `races`, `racesolo <file> [laps] [veh]`, `racehost`, `racestart`, `racecp` (jump to the next checkpoint), `racestat`, `creator <track>`, `crcp`, `crgrid`, `crsave`, `crexit`.
  - netprobe: `racehost` / `racejoin` (run ≥ 125 s).
- **Verified.**
  - Isolated server with two probes: standings and payouts by position (30,000 / 22,500).
  - In game: solo SkyLimit (props, countdown, HUD, finish shard, results, cleanup) and the creator (Afterburner: 149 props, grid, checkpoints, save).
  - Online races in game still need a second player.

## Los Santos Drug Wars: Dax, the Freakshop and the Acid Lab (Gori `AcidLab.inc`, 2026-10-12)

- **Interiors.** DurtyFree mlo.json, mpchristmas3; all four exist in story mode:
  - `xm3_dlc_int_03_xm3` (570,-415,-70) is the Freakshop. Its door is the loading dock under the freeway by the LS River (623,-405,24.6).
  - `xm3_dlc_int_01_xm3` (485,-2625,-50) is the Acid Lab, inside the Brickade 6x6. Floor at about -49.6.
  - int_02 (520,-2625) is a dark hangar with a glass office and int_04 (495,-2560) a white lab/hospital corridor; probably The Last Dose sets.
- **Freakshop.** Walk in at the door. Dax (`ig_dax`) offers "The First Dose", the 6 missions in GTA Online's order, each recreated with Gori's mission steps:
  - Welcome to the Troupe, Designated Driver (Lab Rat `ig_labrat`), Fatal Incursion (the Brickade 6x6 `brickade2`);
  - Uncontrolled Substance (a DrugsMichaelAliensFight trip against clowns), Make War Not Love, Off the Rails (IPL `xm3_train_crash`).
  - Fatal Incursion gives the Acid Lab, like Online.
- **Acid Lab.** Services → Request Acid Lab; press E at the rear to walk in. The interior shows GTA Online's entity sets for the supplies, the product and the equipment upgrade.
  - Lab menu: product (160 units, $1,485 each, +50 % upgraded), supplies, Buy Supplies $60,000, Steal Supplies (a mission, +50 %).
  - Also: Sell Product (deliveries with the Brickade, 1-4 drops), Equipment Upgrade $250,000 (faster, more valuable).
  - Production runs every minute: 0.7 unit (1 upgraded) and 1 % of supplies.
- **Missions are co-op jobs.** Members get half the pay, a quarter of a sale.
- **State** in g_cust `@acid|fd/own/upg/sup/stock10`.
- **Test commands:** `acid [key value]`, `acidclear`, `freak`/`freakout`, `acidlab`/`acidout`, `acidtruck`/`acidret`, `dose <0-5>`.
- **Verified in game.**
  - Freakshop door, interior, Dax and his menu.
  - Mission 1 starts as a co-op job.
  - Acid Lab: the truck, walking in, entity sets visible, the business menu. The test state was cleared afterwards.
  - Dax was created bare: SET_PED_DEFAULT_COMPONENT_VARIATION was added, not seen yet.

## The Last Dose, deathmatches and contact missions (2026-10-12 evening)

- **The Last Dose** (AcidLab.inc). After The First Dose, Dax's menu offers 5 more missions:
  - This Is an Intervention (cult retreat, rescue Lab Rat);
  - Unusual Suspects (the doctor's van at the docks);
  - FriedMind (DrugsTrevorClownsFight trip under the Vinewood sign);
  - Checking In (clinic, `xm3_dlc_int_04_xm3` as `xm3_clinic`);
  - BDKD (the doctor's lab, `xm3_dlc_int_02_xm3` as `xm3_lab`).
  - $50k-$100k each; state `@acid|ld`. Test command: `lastdose <0-4>`.
- **Deathmatches.** Job kind 2, Deathmatch.inc plus server. Same wire format as v10.
  - The host picks an arena (8: Construction Site, Elysian Island Docks, Legion Square, Sandy Shores Airfield, Grapeseed Farm, Paleto Sawmill, Vespucci Beach, Cypress Flats), a time limit (5/10/15 min) and the weapons (Pistols / Rifles / Heavy). Others join from Jobs.
  - Synchronized start and countdown.
  - Each spawn puts the player at a random safe point in the arena with the loadout and full armor, also after each death (LocalOnline respawns YOU at a hospital and Gori puts them back).
  - Leaving the arena: 10 s and then back in.
  - HUD: time left, kills, deaths, position. A results board at the end, and the weapons given are removed again.
  - The server counts kills/deaths in HandleDeath, ranks by kills then deaths, and pays everyone who played by position (payout $10k per 5 min).
  - Verified with netprobe `dmhost`/`dmjoin` on an isolated server: 11 kills / 6 deaths won $20,000, second $15,000. Not in game yet.
- **Contact missions** (ContactMissions.inc). 32 missions with GTA Online's names for Gerald, Simeon, Lamar, Martin, Ron and Trevor.
  - Built from 5 templates: eliminate, steal a vehicle, grab a package, destroy (plant, explosion), rescue.
  - Unlocked by rank, $5k-$18k, co-op jobs.
  - Interaction Menu → Contact Missions, or call the contact (the phone now opens his missions instead of the heists). Test: `cm <contact> <mission>`.
- **Ammu-Nation clerk.** `s_m_y_ammucity_01` / `s_m_m_ammucountry` behind each counter; he turns to face you. Verified at Pillbox Hill.
- **Lester's phone picture.** CHAR_LESTER renders as a 64x64 texture, yet the phone still showed a silhouette. While YOU plays, his charsheet picture label is switched to `CELL_E_311` (CHAR_LESTER_DEATHWISH, Online's picture of him) and restored afterwards. Not verified yet.
- **Deployed 2026-10-13 21:55** (Gori + LocalOnline with kind 2). In game: The Last Dose starts as a co-op job with Dax's text, objective and timer.
  - **Bug found and fixed:** Checking In / BDKD teleported to the MLO origins from DurtyFree's mlo.json (495,-2560,-49 / 520,-2625,-49), which are not floors: the player fell out of the clinic and died. Now `xm3_lab` = 528.25,-2637.73 (fm_content scripts) and `xm3_clinic` = 495,-2566 (floor -50.04, found with the new `intscan <interior> [half-size]` command). Verified in game 2026-10-13: Checking In enters the white clinic and moves on to "Copy the research"; the dark lab has a floor.
  - Contact mission start verified (`cm 0 0`, Gerald's Pier Pressure).
  - Lester: the label swap is applied (`phonechars`: contact 12 → CELL_E_311), but Contacts still shows a silhouette. Still open.

## Survival (job kind 3, Gori `Survival.inc`, 2026-10-13)

- Ten waves at one of 8 places (Stab City, Boneyard, Elysian Island Docks, Galileo Observatory, Paleto Sawmill, Del Perro Pier, Lost MC Clubhouse, Construction Site). Interaction Menu → Survival.
  - Wave n has 8 + 2n enemies, spawned in trickles on the edge of the area (6 + n/2 alive at a time).
  - Tiers: the Lost (waves 1-2), Vagos (3-4), armored black ops with rifles (5-7) and heavier ones (8-10).
  - From wave 3 some arrive by car; waves 5 and 10 add an attack Buzzard.
  - 15 s break between waves (health, armor and ammo refilled). Pistol, SMG, pump shotgun and carbine given at the start and removed at the end.
- **Solo** works without GTA Local Online: dying ends it, cash $50,000 × waves / 10 plus RP.
- **Online:** the host opens a kind-3 job ("S|loc|wave" in the job text), the others join from Jobs until the countdown ends.
  - LocalOnline does not sync NPCs: every player fights the waves on their own game, side by side.
  - Each client reports RaceProgress: checkpoints = its kills, finishMs = waves survived | 0x10000 while down.
  - A player who dies is down until the next wave. The host announces wave n+1 once every player has cleared wave n or is down (2 min grace for stragglers), and passes the job after wave 10 or when everyone is down.
  - The server pays each player payout × their waves / 10 ("Survival: " in the ledger); standings by kills, then waves.
- Server: `kJobSurvival = 3`, `StartsTogether()` (race, deathmatch, survival), wire format unchanged (still protocol v10).
- Verified with netprobe `svhost`/`svjoin` on an isolated server: 7 waves paid $28,000 and 4 waves paid $16,000 (payout $40,000).
- In game 2026-10-13 (host alone, nobody playing): lobby, start, countdown, wave 1 at Stab City with the HUD (wave, players, kills, enemies), death → down → everyone down → job passed, results board, back to the start point. Test commands: `sv <place> [solo]`, `svstart`, `svquit`, `svstat`. Later waves (cars, Buzzard) still need someone playing.

## Transform, special-vehicle and Hotring races (Gori `Races.inc`, 2026-10-14)

- **Transform races.** A race XML can carry `<Transforms><Transform><Checkpoint>i</Checkpoint><VehicleHash>m</VehicleHash></Transform></Transforms>`.
  - Passing checkpoint *i* (0-based) swaps you into vehicle *m* at the same place, heading and speed.
  - Helicopters appear 8 m up and planes 30 m up with the gear raised; on water they would sink and throw you out.
  - Transform checkpoints are purple, with a purple "Transform" blip. The respawn (hold F) uses the vehicle you have at that point.
  - Each client runs its own transforms from the race file, so online races need the same file on every player's game, like any race.
- **Vehicle classes.** In a race's menu, *Vehicle Class* switches between the race's own vehicles, **Special Vehicles** (Rocket Voltic, Ruiner 2000, Blazer Aqua, Scramjet, Vigilante, Oppressor, Deluxo, Stromberg, Toreador) and **Hotring** (Hotring Sabre). Transform races keep their own start vehicle.
- **Race Creator.**
  - *New Race Here (no track)* makes a race anywhere on the map, without stunt props.
  - *Transform At Next Checkpoint* cycles through elegy, bati, blazer, trophytruck, bmx, seashark, dinghy, buzzard2, duster and besra. The next checkpoint you drop gets that vehicle, and is purple with "N > model" over it.
  - Saving writes the `<Transforms>` list.
- **Race payouts like GTA Online.** Online races are paid by the server by position: 100/75/60/50/40/35/30/25 % of 1500 + $100 a checkpoint (max $30,000), and 10 % for not finishing. Solo races now pay too, half of first place (at least $1,000), when you finish. Verified: Vespucci Transform solo paid $1,150 (+380 RP); the test money and RP were reverted.
- **Included race:** `gori-online/races/Gori_Transform_Vespucci.xml`, "Vespucci Transform". ATV on Vespucci Beach, Seashark at the shoreline, along the coast south of Del Perro Pier, then a Buzzard over Del Perro to the finish ring. The route was laid out with the new `probez x y` command (ground and water height).
- Verified in game 2026-10-14 with `racesolo Gori_Transform_Vespucci.xml` and `racecp`: ATV → Seashark in the water → Buzzard flying next to the pier.
  - First try: the Buzzard spawned on the water, sank and threw the player out, and the "MinigameTransitionIn" effect kept the screen blurred.
  - Fixed with the spawn height and the short "RaceTurbo" flash.

## The newer businesses (Gori `Ventures.inc`, 2026-10-14)

GTA Online's 2023-2025 businesses, bought on Real Estate (Maze Bank Foreclosures) like the others.

| Business | Locations | Inside | Jobs and income |
|---|---|---|---|
| **Salvage Yards** (The Chop Shop) | Paleto Bay, Sandy Shores, La Puerta, Strawberry, Murrieta Heights | `m23_2_dlc_int_salvage` (Red's Pickup and Auto Salvage Yard) | *Salvage Yard Robbery*: steal a high-end car from a gang garage, bring it in and salvage it ($120k-$220k). Safe: the tow truck, $18k a day. |
| **Bail Offices** (Bottom Dollar Bounties) | Mission Row, West Vinewood, Del Perro, Davis, Paleto Bay | `m24_1_dlc_int_bounty`, office floor (a storeroom below) | *Bounty Target*: bring a bail jumper in alive to Mission Row police station ($30k-$60k). Safe: the staff, $15k a day. |
| **Garment Factory** (Agents of Sabotage) | La Mesa (Lester's old factory) | `m24_2_int_hacker_basement`, the Hacker Den | *Sabotage Job*: blow up a contractor's servers ($45k-$80k). Safe $12k a day. |
| **Hands On Car Wash**, **Smoke on the Water**, **Higgins Helitours** (Money Fronts) | Strawberry, Vespucci, Vespucci Heliport | `m25_1_int_tycoon_car_wash` (`set_carwash_tints`); `m25_1_int_tycoon_smoke_heli` dressed as the dispensary (`set_smoke`) or the tour office (`set_heli`) | Safes of $9k / $11k / $14k a day. |

- **Entrances:** freemode.ysc's simple interior locates (ids 162-176; the angled area's two corners).
  - The first visit checks for real ground at the door. If there is none (awnings, shop fronts without collision in story mode), it moves the entrance to the nearest ground within 7 m and keeps it there.
  - Leaving walks you to the nearest pavement (navmesh) instead of straight ahead.
- **Interiors:** the entity sets come from Online's own scripts (am_mp_salvage_yard, am_mp_bail_office, am_mp_sb_car_wash / _weed_shop / _heli_tours): salvage yard lifts, mechanic, safe and tint upgrades; the bail office staff; the Money Fronts sets. Without the exterior IPLs loaded, the Money Fronts MLOs had no walls. Interior sets can now be switched off with `!name`, because the dispensary and the tour office share one MLO.
- **Exterior IPLs:** the salvage yard signs (`m23_2_*_reds`), the bail office fronts (`m24_1_bailoffice_*`), `m24_2_garment_factory_door`, `m25_1_carwash`, `m25_1_helitours` and `m25_1_smokeonthewater` are loaded with the online map.
- **Prices** are close to Online's, not exact (their tunables are hashed).
- **Verified in game 2026-10-14:**
  - The interiors (salvage, bail office, Hacker Den) and the three job starts (`venture salvage|bounty|sabotage`).
  - Buying, travelling, entering and leaving with E at Bail Office: Davis and Salvage Yard: Strawberry.
  - Fixed on the way: a roof landing, then a fall through a shop front and a fall through the salvage door.
  - The test purchases were undone with `delprop`.

## Roadmap: GTA Online content (user request 2026-10-12)

GTA Online's own jobs are cloud UGC run by `fm_mission_controller` inside a real network session, so they cannot run in story mode. They are recreated in Gori, multiplayer through the co-op jobs and races above.
- **Done.**
  - Races: 104 land races, plus the Race Creator for the 129 stunt tracks.
  - Gori's heists, business and client missions (co-op jobs).
  - LS Customs and Ammu-Nation.
  - Drug Wars: The First Dose and the Acid Lab.
- **Done in the evening.** The Last Dose, deathmatches, 32 contact missions.
- **Done 2026-10-13.** Survival (solo and co-op).
- **Done 2026-10-14.** Transform races, special-vehicle and Hotring classes, Race Creator anywhere on the map; Salvage Yards, Bail Offices, Garment Factory and the Money Fronts businesses.
- **Next.**
  - Stunt race checkpoints for the 129 stunt tracks (the Race Creator does it by hand today).
  - More contact missions (Lester, Martin's later ones, Agent 14...).
  - The businesses' own heists (Salvage Yard robbery finales, bail office staff missions).

## Co-op jobs (protocol v9, 2026-10-12)

Any Gori Online mission started by the YOU character becomes a server job that other players can join.
- **Protocol.** JobOpen (70), JobList (71), JobJoin (72), JobUpdate (73) and JobState (74).
  - The host's game runs the mission and reports it: JobOpen, then JobUpdate (`status`, `step`, objective position and text). Status runs lobby 0 → running 1, and ends at passed 2 / failed 3 / closed 4.
  - The server relays JobState to the host and the members, and JobList to everyone.
  - Limits: one job per host, 8 members, and the server only accepts updates from the host.
- **Payout.** When a job passes, the server pays each member's bank: AdjustAccounts plus an unsolicited MoneyResult (requestId 0).
  - It is capped by `jobs.max_payout` (default 2,000,000).
  - A job that passes under `jobs.min_duration_ms` (default 60 s, counted from the first running update) pays nothing.
  - Members can join a job that is already running.
- **Client.** `game/jobs.cpp`, with LocalOnline API v5 exports for Gori:
  - `LocalOnline_JobOpen/JobUpdate/JobJoin/JobRevision/GetJobs/GetJobState`.
  - Texts are tab-separated, and the host name is `you` for our own job.
- **Gori (`Coop.inc`).**
  - StartMission → CoopOpen. `Mission::coopPay` is a member's cut:
    - client job or security contract: half the pay;
    - heist finale: a quarter; heist prep: $5,000;
    - anything else: 4000 + 3000 × steps.
  - MissionTick reports each step and its objective, which follows the target car on steal steps. Pass, fail and profile switch end the job.
  - Members get:
    - a notification for each new job, and Interaction Menu → Jobs to join or leave;
    - a yellow objective blip with a GPS route, plus the host's objective text;
    - shards on start, pass (with the cut) and fail.
  - Members only follow and fight; the host's game decides the objectives.
- **Test commands.** Gori: `testjob` (3 walk-to points 12 m apart, $5,000), `jobs` (logs list and state), `jobjoin <id>` (0 = leave). netprobe: modes `jobhost` (opens "Probe Heist" at $25,000 and passes it at 80 s; run ≥ 90 s) and `jobjoin`.
- **Status.** Deployed 2026-10-12 (server v9, LocalOnline API v5, Gori with `Coop.inc`).
  - netprobe host→joiner on an isolated server: every step was relayed and the joiner was paid $25,000.
  - In game, as host: `testjob` opens the job, reports each step, and passes it ("passed too quickly: no payout" with no members). Quitting reports failed. Interaction Menu → Jobs lists the jobs.
  - Not tested yet: the member side in game (needs a second player). Running netprobe against the live server is refused by Claude Code's permission check, because it would create a test account in the real DB.

## Time

- The server tick defaults to 20 Hz (`tick_rate`).
- `serverTimeMs` in Welcome and Snapshot gives clients a shared clock for the interpolation buffer. The offset is estimated from ENet's RTT.

## Testing without two GTA installs

`tools/netprobe` is a fake client that walks in a circle at LSIA at 20 Hz. Run it on any PC (it builds natively on Linux too) to get a "second player" while only one GTA client exists.

Proven on 2026-10-07:
- BC-250 → Windows: RTT 1 ms, 0 % loss.
- Two probes see each other (`PlayerJoined`, snapshot count 1).
