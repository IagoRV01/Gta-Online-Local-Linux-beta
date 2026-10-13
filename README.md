# GTA Local Online (Beta)

**Un "GTA Online" privado para tu red local**, construido sobre el modo historia de GTA V Legacy:
un servidor propio en Windows, un cliente `.asi` en cada PC que juega y el mod **Gori Online** con el
contenido (personaje freemode, dinero, propiedades, negocios, golpes, carreras, deathmatch, Survival,
misiones de contacto, Drug Wars, LS Customs, Ammu-Nation, teléfono de Online...).

Sin servidores de Rockstar, sin Internet y sin cuentas de terceros: todo vive en tu LAN.

> ⚠️ No es GTA Online ni se conecta a él. Necesitas tu propia copia legal de **GTA V Legacy** (Steam,
> Rockstar o Epic). Este repositorio no contiene ningún archivo del juego. Úsalo solo en modo historia:
> ScriptHookV ya impide cargar mods en GTA Online.

**➡️ Guía paso a paso desde cero: [docs/GUIA-INSTALACION.md](docs/GUIA-INSTALACION.md)**

---

## Cómo funciona

```
 PC servidor (Windows)                         PCs que juegan (Windows o Linux con Proton)
 ┌──────────────────────────┐   LAN, UDP 4499  ┌─────────────────────────────────────────┐
 │ LocalOnlineServer.exe    │ ◄──────────────► │ GTA V Legacy 1.0.3889 + ScriptHookV      │
 │  · cuentas y personajes  │      (ENet)      │  · LocalOnline.asi  (red, personaje YOU, │
 │  · dinero (libro mayor)  │                  │    otros jugadores, dinero, garaje)      │
 │  · trabajos y carreras   │                  │  · GoriOnline.asi   (el contenido de     │
 │  · hora y clima comunes  │                  │    Online: menús, misiones, negocios...) │
 │  · SQLite (data/*.db)    │                  └─────────────────────────────────────────┘
 └──────────────────────────┘
```

- El **servidor** no necesita GTA: puede ir en cualquier PC con Windows de la red (también en uno de
  los que juegan).
- Cada jugador ve a los demás, sus coches y su ropa; el dinero, los perfiles y los pagos de los trabajos
  los guarda el servidor.
- Hasta 8 jugadores (configurable).

## Qué incluye

| Parte | Qué hace |
|---|---|
| `LocalOnline.asi` | Pantalla de inicio *Story Mode / Local Online*, creador de personaje freemode, sincronización de jugadores y vehículos, PvP validado por el servidor, banco Maze Bank, HUD de Online, garaje, trabajos cooperativos. Menú: **F10**. |
| `GoriOnline.asi` | Menú de interacción de Online (**M**, o mantener *Back/Select* en el mando): propiedades y negocios, golpes, misiones de contacto, carreras (y creador de carreras), deathmatch, Survival, The First Dose / The Last Dose y el Acid Lab, LS Customs, Ammu-Nation, vehículos especiales, yate, casino, Internet y teléfono de Online. |
| `LocalOnlineServer.exe` | Servidor autoritativo: cuentas, personajes, economía con límites anti-trampas, trabajos, carreras, deathmatch, Survival, hora y clima compartidos. |
| `netprobe.exe` | Cliente de prueba sin GTA para comprobar que la red llega al servidor. |

**Limitaciones conocidas:** los NPC no se sincronizan entre jugadores (en Survival y en las misiones
cada uno lucha contra sus propios enemigos, a la vez y en el mismo sitio); solo funciona con la versión
**1.0.3889** de GTA V Legacy (no con GTA V Enhanced); los trabajos de Online son recreaciones, no los
originales de Rockstar.

## Inicio rápido

1. Descarga la última [release](../../releases): `GTALocalOnline-Server-*.zip` y `GTALocalOnline-Client-*.zip`.
2. **Servidor (un PC Windows):** descomprime, copia `server.example.toml` a `server.toml`, abre el
   cortafuegos con `setup-firewall.ps1` y ejecuta `start-server.bat`. Apunta su IP (`ipconfig`).
3. **Cada PC que juega:** instala [ScriptHookV](http://www.dev-c.com/gtav/scripthookv/) para la versión
   3889, copia `LocalOnline.asi`, `GoriOnline.asi` y la carpeta `LocalOnline\` a la carpeta de GTA V, y pon
   la IP del servidor y tu nombre en `LocalOnline\client.toml`.
4. En Linux/Steam Deck añade en Steam las opciones de lanzamiento
   `WINEDLLOVERRIDES="dinput8,xinput1_4=n,b" %command%`.
5. Abre GTA V y elige **LOCAL ONLINE** en la pantalla de inicio.

Todos los detalles, capturas de qué esperar y solución de problemas: **[docs/GUIA-INSTALACION.md](docs/GUIA-INSTALACION.md)**.

## Compilar desde el código

Necesitas Windows con [w64devkit](https://github.com/skeeto/w64devkit) (MinGW g++ 14 o superior), Python 3 y
Git Bash (o la propia shell de w64devkit):

```bash
export W64DEVKIT=/c/ruta/a/w64devkit/bin   # o pon su bin/ en el PATH
./build.sh          # tests + LocalOnline.asi + LocalOnlineServer.exe + netprobe.exe
./build.sh gori     # GoriOnline.asi
```

Los binarios quedan en `build/`. No hace falta Visual Studio ni el SDK de ScriptHookV: los `.asi` enlazan
ScriptHookV en tiempo de ejecución con `GetProcAddress`.

## Estructura

```
client/gta-plugin/   LocalOnline.asi (C++20)
client/linux/        deploy.sh: copia el cliente a un PC Linux por SSH
gori-online/         GoriOnline.asi (C++20) y sus generadores de datos (tools/)
server/              LocalOnlineServer.exe, esquema SQLite y scripts de Windows
shared/              protocolo de red y código común cliente/servidor
config/              server.example.toml y client.example.toml
tests/               tests unitarios (se ejecutan en cada ./build.sh)
tools/netprobe/      cliente de prueba de red
docs/                guía de instalación y documentación técnica
third_party/         ENet y SQLite (ver THIRD_PARTY.md)
```

Documentación técnica: [arquitectura](docs/architecture.md), [protocolo](docs/protocol.md),
[decisiones](docs/decisions.md), [multijugador y contenido](docs/multiplayer.md), [Proton](docs/proton.md),
[problemas conocidos](docs/troubleshooting.md).

## English summary

A private, LAN-only "GTA Online" for GTA V Legacy (1.0.3889) story mode: a Windows game server
(`LocalOnlineServer.exe`), a ScriptHookV client plugin (`LocalOnline.asi`) and a content mod
(`GoriOnline.asi`) that recreates GTA Online's freemode, properties, businesses, heists, races,
deathmatches, survivals and contact missions. Download the release, run the server, drop the two `.asi`
files plus ScriptHookV into each game folder and point `LocalOnline\client.toml` at the server. The full
step-by-step guide is in Spanish: [docs/GUIA-INSTALACION.md](docs/GUIA-INSTALACION.md). No Rockstar files
are included; you need your own copy of the game.

## Licencia

[MIT](LICENSE). ENet (MIT) y SQLite (dominio público) mantienen sus licencias: ver [THIRD_PARTY.md](THIRD_PARTY.md).
GTA V y sus marcas pertenecen a Rockstar Games / Take-Two; este proyecto no está afiliado a ellos.
