# Troubleshooting

| Symptom | Check | Fix |
|---|---|---|
| No "GTA Local Online" notification in game | `ScriptHookV.log`: is there a `Launching main() for 'LocalOnline.asi'` line? | Launch options must include `WINEDLLOVERRIDES="dinput8,xinput1_4=n,b" %command%`; ScriptHookV must match the game build (3889) |
| F10 does nothing | `LocalOnline\LocalOnline.log` | Another mod may be eating the key: change `menu_key` in `client.toml` |
| "Not available during a mission" | a story mission or minigame is active | finish or abandon it |
| Character looks wrong or default | log lines `[profile]` | `character.json` failed validation; the `.bak` copy loads automatically. Fix or delete the file to recreate. |
| Back in story mode in the wrong place | log `story ped ... is gone` | a game script removed the hidden story ped. The fallback rebuilds it at the saved spot with the captured outfit. Report it with the log. |
| Story save looks changed | — | restore from `LocalOnline\backups\saves-<date>\` or `~/LocalOnline-backups/` with GTA closed (see proton.md) |
| netprobe: "no UDP answer" | is the server running? Is the address right? | Use the **wired** IP 192.168.1.10, not Wi-Fi .140; run `server/windows/setup-firewall.ps1` |
| Server: "cannot bind UDP" | another process on 4499 | `Get-NetUDPEndpoint -LocalPort 4499`; change `game_port` in both TOMLs |
| GTA stuck on "Entering Story Mode", ScriptHookV.log ends at "Waiting to launch" | leftover Rockstar Launcher / RockstarService processes after several quick restarts (seen 2026-10-08 00:04) | close GTA, `pkill -f Launcher.exe; pkill -f RockstarService`, start again |
| "Server offline - playing from the local save" | the Windows server is not running / not reachable | start it; the plugin retries every 30 s and uploads the offline progress (`[sync] uploaded (reconnect)`) |
| "wrong token for <name>" | the account exists but `LocalOnline\cache\token` is missing/different | restore the token file, or delete the account row on the server (admin) to re-register |
| Very long loads, stutter, Steam/GTA closed by itself; `journalctl` shows `systemd-oomd killed ... app-steam` | the BC-250 has too little RAM: the BIOS reserves too much for the GPU (`cat /sys/class/drm/card0/device/mem_info_vram_total` → 8192 MB) | BIOS → Chipset → GFX Configuration → UMA Frame Buffer Size → 512M (or ≤ 2G); `free -h` should show ~14 GiB |
| Landing page appears and ONLINE is pressed | ScriptHookV blocks official Online ("altered version of the game") | press OK: story mode loads and the Local Online start screen appears (Local Online preselected) |
| Crash with LocalOnline in the call stack | `LocalOnline.log` `[crash]` lines with `LocalOnline.asi+0x…` | `addr2line -e build/LocalOnline.dbg.asi 0x<offset+0x140000000>` (w64devkit) |

Log format everywhere: `YYYY-MM-DD hh:mm:ss.mmm LEVEL [component] [player/entity] event`.
Set `debug = true` (client `[debug]`, server `[logging]`) for DEBUG lines.

## No minimap, no pause map (ESC), no weapon wheel (TAB) or radio wheel (Q) after dying as YOU

Cause: the story `respawn_controller` script hides the HUD and slows time on death and only undoes
it after its own fade-out; Local Online resurrects YOU before that. Fixed 2026-10-09: the plugin
restores HUD, radar, time scale, post-FX and the death audio scene after its respawn (and keeps
doing it for 8 s). If it ever happens again: remote command `hudstate` logs the state, `hudfix`
restores it.

## SQLite backups

The server database runs in WAL mode: most recent data lives in `localonline.db-wal` until a
checkpoint. Never copy only `localonline.db`. Copy all three files with the server stopped, or use
the SQLite backup API on the live file (for example Python `sqlite3.connect(src).backup(dst)`).

## The client disconnects and reconnects every 20–40 s

Seen 2026-10-09 right after loading on the BC-250 with the 8 GB VRAM carve-out: 6.7 GB in swap,
the game froze for seconds and the server timed the client out (5–10 s). Timeouts are now 30 s on
both sides (`network.timeout_ms = 30000`, client `enet_peer_timeout(15000, 30000)`), and the client
logs `game froze for N ms` for any freeze over 2.5 s. The real fix is the BIOS memory split
(UMA_SPECIFIED 512 MB), which only the owner can change.

## The client says "server not reachable" after the Windows PC rebooted

The server does not start by itself yet, and the PC's address comes from DHCP: on 2026-10-09 it
changed from 192.168.1.10 to 192.168.1.10. Start `build/server-run/LocalOnlineServer.exe`, check
the address with `Get-NetIPAddress`, and set `address` in the BC-250's `LocalOnline/client.toml`
(the client reads it at start). A DHCP reservation in the router avoids this.

## After dying as YOU: no minimap, ESC does nothing (fixed 2026-10-10)

Two causes, both seen live with the `hudstate` diagnostics:
1. `main.ysc` relaunches `respawn_controller` on every death (func_1898: player injured and the
   script not running). It plays the story hospital restart and hides the HUD frame by frame. It is
   now stopped every time it reappears while YOU is active (`respawn_controller relaunched by the
   story (death): stopped again`), and what its first frames changed is undone.
2. The plugin keeps the game's death restart paused (`PAUSE_DEATH_ARREST_RESTART`) so it cannot
   rebuild the story character before our own respawn. While it stays pending the game treats the
   player as WASTED: `IS_MINIMAP_RENDERING` is 0 and the pause menu is disabled, although HUD and
   radar report "not hidden". After our respawn the restart is now allowed to finish for ~8 s; it
   rebuilds the ped with YOU's model and the plugin re-applies look and weapons.
Test commands: `hudstate` (player, switch, cutscene, minimap, script camera state), `try.nopause`,
`try.stateofplay`, `try.playing`.
