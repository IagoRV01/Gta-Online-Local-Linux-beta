# GTA V modding under Proton (BC-250) — what works and how

## Working setup (verified 2026-10-07)

- **Game:** GTA V Legacy 1.0.3889, Steam appid 271590, under **Proton Experimental** (11.0-20261001), prefix `compatdata/271590`.
- **ScriptHookV** v3889 sits in the game folder, together with the `dinput8.dll` ASI loader.
- **Steam launch options:**
  ```
  WINEDLLOVERRIDES="dinput8,xinput1_4=n,b" %command%
  ```
  Without the override Wine uses its builtin `dinput8`, and no `.asi` loads.
- **Native C++ `.asi` mods built with MinGW load and run.** Examples: LocalOnline.asi, GTAResort.asi, GoriOnline.asi.
  - Bind ScriptHookV with `GetProcAddress` and MSVC-mangled names (`client/gta-plugin/src/engine/shv.cpp`). No SDK `.lib` is needed.
  - Link statically: `-static -static-libgcc -static-libstdc++`, so no MinGW runtime DLLs are needed in the prefix.

## Not working

- **ScriptHookVDotNet**: crashes at story load on Wine Mono. `.NET 4.8` fails to install in this prefix. Keep `ScriptHookVDotNet.asi.disabled`.
- **FiveM / RAGE:MP**: their integrity and anti-cheat layers fail under Wine. Bypassing them is not an option.

## Rules for touching the game or the prefix

1. **Back up first.**
   - Story saves: done once in `~/LocalOnline-backups/`, and per session by the plugin.
   - Plugin: `deploy.sh` keeps `LocalOnline.asi.bak-<date>`.
   - Prefix (if ever needed): `cp -a compatdata/271590 ~/LocalOnline-backups/prefix-<date>` (about 1–2 GB).
2. **Never install runtimes into the live prefix to experiment.** Copy it, point a test Steam launch at the copy (`STEAM_COMPAT_DATA_PATH`), and experiment there.
3. GTA loads `.asi` files only at startup. Deploying while it runs is safe; the new version loads on the next start.
4. `deploy.sh` refuses nothing while the game runs (copying is harmless). Restarting GTA is always a human decision: the user may be playing.

## Rollback

```
client/linux/deploy.sh --rollback   # previous LocalOnline.asi
client/linux/deploy.sh --remove     # disable the plugin entirely (rename to .disabled)
```

To restore story saves, copy `LocalOnline-backups/saves-<date>/<profile>/SGTA5*` back to `…/pfx/drive_c/users/steamuser/Documents/Rockstar Games/GTA V/Profiles/<profile>/` with the game closed.
