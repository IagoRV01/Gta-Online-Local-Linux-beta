# Installation guide, from scratch

*Versión en español: [GUIA-INSTALACION.md](GUIA-INSTALACION.md).*

This guide sets up the whole of **GTA Local Online** at home: one server and one or more PCs playing on the same
local network. No programming needed: you download files, copy them and edit two text files. Allow about 30 minutes
the first time.

**Contents**
1. [What you need](#1-what-you-need)
2. [Set up the server (Windows)](#2-set-up-the-server-windows)
3. [Prepare each PC that plays (Windows)](#3-prepare-each-pc-that-plays-windows)
4. [Prepare a PC that plays on Linux, Steam Deck or Bazzite](#4-prepare-a-pc-that-plays-on-linux-steam-deck-or-bazzite)
5. [First session](#5-first-session)
6. [Optional content: races](#6-optional-content-races)
7. [Running the server](#7-running-the-server)
8. [Updating](#8-updating)
9. [Troubleshooting](#9-troubleshooting)
10. [Building it yourself](#10-building-it-yourself)
11. [Optional: GodEye](#11-optional-godeye)

---

## 1. What you need

| What | Details |
|---|---|
| **GTA V Legacy** | The classic version (Steam app 271590, Rockstar Launcher or Epic), updated to **1.0.3889**. GTA V *Enhanced* does not work. Every player needs their own copy. |
| **ScriptHookV for 3889** | From [dev-c.com](http://www.dev-c.com/gtav/scripthookv/) (Alexander Blade). It brings `ScriptHookV.dll` and `dinput8.dll` (the `.asi` loader). It is not included here: its license does not allow redistribution. |
| **A Windows 10/11 PC for the server** | It can be one of the PCs that play. The server needs neither GTA nor a graphics card, and uses very little. |
| **A local network** | Every PC on the same network. Cable is better: some mesh Wi-Fi networks isolate devices from each other. |
| **The release** | On the repository's [Releases](../../../releases) page: `GTALocalOnline-Server-<version>.zip` and `GTALocalOnline-Client-<version>.zip`. |

> **Which GTA version do you have?** Right-click `GTA5.exe` → *Properties* → *Details*. *Product version* must be
> **1.0.3889.0**. When Rockstar releases an update, ScriptHookV stops working until its author releases a new
> one, and these `.asi` files may need an update too.

---

## 2. Set up the server (Windows)

1. **Unzip** `GTALocalOnline-Server-<version>.zip` into a fixed folder, for example `C:\GTALocalOnline\`. Inside:
   ```
   LocalOnlineServer.exe   the server
   server.example.toml     example configuration
   start-server.bat        starts the server
   setup-firewall.ps1      opens the port in Windows Firewall
   netprobe.exe            network test
   ```
2. **Make the configuration:** copy `server.example.toml` and rename the copy to `server.toml`. The defaults work;
   the most useful values:
   ```toml
   [server]
   name = "GTA Local Online"      # your server's name
   [network]
   game_port = 4499               # UDP port
   max_players = 8
   [accounts]
   allow_registration = true      # each new name makes its account the first time it joins
   [combat]
   pvp = true                     # players can hurt each other
   ```
3. **Open the firewall:** right-click `setup-firewall.ps1` → *Run with PowerShell* (it asks for administrator
   rights). It makes a rule that allows **UDP 4499 from your local network only**.
   - If Windows blocks the script, open PowerShell in that folder and run:
     `powershell -ExecutionPolicy Bypass -File .\setup-firewall.ps1`
   - To remove the rule later: `Remove-NetFirewallRule -DisplayName 'GTA Local Online (UDP)'`
4. **Start the server:** double-click `start-server.bat`. You should see something like:
   ```
   INFO  [net] listening on UDP 0.0.0.0:4499, tick 20 Hz, max 8 players, protocol v13
   ```
   The first time, Windows may ask whether to allow network access: allow it for **private networks**. Keep the
   window open while you play (closing it stops the server).
5. **Note the server's IP:** open `cmd` and type `ipconfig`. Look for the *IPv4 Address* of the **Ethernet**
   adapter (if you use a cable) or the Wi-Fi one, for example `192.168.1.10`.
   - Recommended: in your router, **reserve that IP** for this PC (static DHCP). Otherwise it can change after a
     restart, and you will have to edit every `client.toml` again.

The data is kept in `data\localonline.db` (accounts, characters, money) and the logs in `logs\game-server.log`,
next to the `.exe`.

**Starting automatically (optional):** Windows Task Scheduler → *Create Basic Task* → *When I log on* → *Start a
program* → `start-server.bat` (set *Start in* to the server's folder).

---

## 3. Prepare each PC that plays (Windows)

Find the GTA V folder (the one with `GTA5.exe`). In Steam: right-click the game → *Manage* → *Browse local files*.

1. **ScriptHookV:** download the version for 3889 and copy from its `bin\` folder into the GTA V folder:
   - `ScriptHookV.dll`
   - `dinput8.dll`
   - (`NativeTrainer.asi` is not needed; if you copy it, its F4 key gets in no one's way).
   > Windows Defender sometimes deletes `ScriptHookV.dll` when you unzip it. If it does, restore it from *Virus &
   > threat protection → Protection history*, or add an exclusion for the game folder. That is your decision:
   > only do it if you trust the file you downloaded.
2. **The mods:** unzip `GTALocalOnline-Client-<version>.zip` and copy **everything** into the GTA V folder:
   ```
   LocalOnline.asi
   GoriOnline.asi
   LocalOnline\client.toml
   ```
3. **Configure the client:** open `LocalOnline\client.toml` in Notepad and change two lines:
   ```toml
   [server]
   address = "192.168.1.10"   # the server's IP from step 2.5 (127.0.0.1 if the server is this same PC)
   port = 4499

   [player]
   name = "YourName"          # your name for the others: 2-24 letters, digits, spaces, . - _
   ```
   The name is your account: the first time you join, the server makes it and stores a secret *token* in
   `LocalOnline\cache\`. If you move to another PC with the same name, copy that `cache` folder too.
4. Start GTA V as usual from Steam, Rockstar or Epic.

To uninstall: delete `LocalOnline.asi`, `GoriOnline.asi` and the `LocalOnline\` folder (and ScriptHookV, if you
don't use it for anything else). Your story mode save is never touched.

---

## 4. Prepare a PC that plays on Linux, Steam Deck or Bazzite

GTA V Legacy runs under **Proton**, and so do the `.asi` files. It has been tested with *Proton Experimental*.

1. Do the same as in [step 3](#3-prepare-each-pc-that-plays-windows): ScriptHookV (`ScriptHookV.dll`,
   `dinput8.dll`), `LocalOnline.asi`, `GoriOnline.asi` and `LocalOnline/client.toml` in the game folder, usually
   `~/.local/share/Steam/steamapps/common/Grand Theft Auto V/`.
2. In Steam: right-click GTA V → *Properties* → *General* → **Launch options**:
   ```
   WINEDLLOVERRIDES="dinput8,xinput1_4=n,b" %command%
   ```
   Without it, Wine uses its own `dinput8` and **no `.asi` loads**.
3. Under *Compatibility*, force **Proton Experimental** (or another recent version).
4. Don't install ScriptHookVDotNet: under Wine Mono it hangs while story mode loads (and it isn't needed).

To copy the files from a Windows PC over SSH, the source code has `client/linux/deploy.sh` (variables
`BC250_HOST` / `BC250_IP`: the SSH alias and the IP of the Linux PC).

---

## 5. First session

1. With the server on, open GTA V. After loading, the Local Online start screen appears:
   - **STORY MODE**: normal story mode, offline.
   - **LOCAL ONLINE**: join with your Online character.
2. The first time, the **character creator** opens (looks, clothes, name). Then you appear in Los Santos as in
   Online: with your money, your rank and your clothes, all kept on the server.
3. Keys:
   | Key | What it opens |
   |---|---|
   | **M** (or hold *Back/Select* on a controller) | Gori Online's interaction menu: properties, businesses, heists, contact missions, races, Survival, deathmatch, vehicles, Internet... |
   | **F10** | The LocalOnline menu: connection status, players, options. Change it in `client.toml` (`menu_key`). |
   | **Up arrow** | The Online phone (contacts, Internet, jobs). |
4. **Playing together:** when someone opens a job, a race, a deathmatch or a Survival, everyone else gets a notice.
   To join: interaction menu (**M**) → **Jobs**. The host starts it from their menu once everyone is in; races,
   deathmatches and Survivals start with a shared countdown.
5. Job payouts come from the server, straight into each player's bank. Races pay as in Online: online, by your
   position; alone, half of what the winner would get (at least $1,000).

> Tip: don't stay inside an interior or under water when you quit the game: you appear where your position was
> saved.

---

## 6. Optional content: races

Gori Online reads races in the XML format of **Community Races** (the race mod on GTA5-Mods):

- Put the `.xml` files in `GoriOnline\Races\` inside the GTA V folder (make the folders if they don't exist). They
  show up in the interaction menu → **Races**, and on the map as race flags.
- For tracks without checkpoints (*stunt* maps in Map Editor XML format with `<Metadata>`), put them in
  `GoriOnline\Tracks\` and use **Races → Race Creator**: drive the track, mark the checkpoints with **E**, place the
  grid and save it as a race.
- **Transform races:** some (purple) checkpoints swap your vehicle at the same speed: from quad to jet ski, from jet
  ski to helicopter... The repository comes with one made for this project: copy
  `gori-online/races/Gori_Transform_Vespucci.xml` to `GoriOnline\Races\`.
- **Special vehicles and Hotring:** in any race's menu, *Vehicle Class* swaps the race's cars for Online's special
  vehicles (Rocket Voltic, Ruiner 2000, Blazer Aqua, Scramjet...) or for the Hotring cars.
- **Make your own races anywhere:** Races → Race Creator → *New Race Here (no track)*. Mark checkpoints with **E**;
  *Transform At Next Checkpoint* picks the vehicle the next checkpoint turns you into.

Other races are not included in this repository: download them from their authors (for example race and circuit
packs on GTA5-Mods), respecting their licenses.

---

## 7. Running the server

- **Live commands:** write a line in the file `server.cmd` (next to the `.exe`); the server reads it within a second
  and empties it:
  ```
  time 22 30          sets the time to 22:30 for everyone
  freeze on|off       freezes the clock
  weather RAIN        fixed weather (CLEAR, CLOUDS, RAIN, THUNDER, FOGGY, SNOW...)
  weather auto        back to automatic weather
  blackout on|off     a citywide blackout
  ```
- **Closing account registration:** once everyone has an account, set `allow_registration = false`.
- **Economy limits** (`[economy]` in `server.toml`): the maximum per transaction, the maximum income per minute and
  the starting money. They protect against mods or bugs that would make endless money.
- **Backups:** the database uses WAL mode, so copying only `localonline.db` while the server runs can give an empty
  copy. Stop the server and copy the whole `data\` folder (`localonline.db`, `localonline.db-wal`,
  `localonline.db-shm`).
- **Testing the network without GTA:** from any Windows PC on the network:
  ```
  netprobe.exe 192.168.1.10 4499 Test 5
  ```
  If it answers with an RTT and 0 % loss, the server can be reached. Note: it makes an account called "Test".

---

## 8. Updating

1. Download the new release.
2. Server: close its window, replace `LocalOnlineServer.exe` and start it again. `server.toml` and `data\` are kept
   (copy `data\` first, just in case).
3. Clients: with GTA closed, replace `LocalOnline.asi` and `GoriOnline.asi`.
4. The server and the clients must come from the **same release**: when the protocol changes, the server turns old
   clients away with a notice.

---

## 9. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| The *Story Mode / Local Online* screen doesn't appear | The `.asi` files don't load. Windows: check that `dinput8.dll` and `ScriptHookV.dll` are next to `GTA5.exe` and that the game is 1.0.3889. Linux: the `WINEDLLOVERRIDES` launch options are missing. Look at `ScriptHookV.log` in the game folder. |
| ScriptHookV says *"Unknown game version"* | Rockstar updated GTA V. Wait for the new ScriptHookV (and for new versions of these mods if needed). |
| "Server not reachable" / can't connect | 1) Is `start-server.bat` open? 2) Is the IP in `client.toml` the server's `ipconfig` one? (It changes after a restart unless you reserved it in the router.) 3) Did you run `setup-firewall.ps1`? 4) Try `netprobe.exe`. 5) If the server is on Wi-Fi in a mesh network, try a cable. |
| It disconnects and reconnects every so often | Unstable Wi-Fi, or the PC freezes for a few seconds (low memory). A cable is better. The client reconnects on its own and syncs your progress again. |
| Linux: GTA closes a second after pressing *Play* | Check `journalctl --user` for `pressure-vessel` / `capsule-capture-libs`. If the Steam runtime (SteamLinuxRuntime) is at fault, try *Verify integrity* on *SteamLinuxRuntime_4* in the Steam Library (Tools), or restart the PC. |
| No minimap or pause menu after dying | Wait a few seconds: story mode finishes its death sequence; the client handles it. |
| More | [troubleshooting.md](troubleshooting.md). |

The logs to share when asking for help: `LocalOnline\LocalOnline.log` and `GoriOnline.log` (game folder) and
`logs\game-server.log` (server).

---

## 10. Building it yourself

Only if you want to change the code; the release comes fully built.

1. Install on Windows:
   - [w64devkit](https://github.com/skeeto/w64devkit/releases) (unzip it to, for example, `C:\w64devkit`).
   - [Python 3](https://www.python.org/downloads/).
   - [Git](https://git-scm.com/download/win) (it brings Git Bash).
2. Clone and build from Git Bash (or by opening `w64devkit.exe`):
   ```bash
   git clone <this repository's url>
   cd gta-local-online
   export W64DEVKIT=/c/w64devkit/bin
   ./build.sh          # tests + LocalOnline.asi + LocalOnlineServer.exe + netprobe.exe
   ./build.sh gori     # GoriOnline.asi
   ```
3. The result is in `build/`. For the server, copy `build/LocalOnlineServer.exe`, `config/server.example.toml` and
   `server/windows/*` into a folder of their own, as in step 2.

Optional: if you add new native functions, download `natives.json` from
[alloc8or/gta5-nativedb-data](https://github.com/alloc8or/gta5-nativedb-data) into `client/gta-plugin/tools/` and
`gori-online/` so the build regenerates the native headers.

---

## 11. Optional: GodEye

GodEye is a web app that runs on the server PC next to the game server:
- live 2D and 3D maps;
- camera clients with live video;
- an asymmetric web game: hackers, police dispatch and operators play against the players without GTA.

Its own guide is [godeye/README.md](../godeye/README.md). The short version:
1. Install Node.js 24 and start GodEye (`GodEye.exe`, or `node src/main.ts` in `godeye/backend`).
2. Open it on the server PC.
3. Click **Set up GodEye** to make the first account: the ADMIN, with the control panel.
