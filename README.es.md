# GTA Local Online (Beta)

*English: [README.md](README.md).*

**Un "GTA Online" privado para tu red local**, construido sobre el modo historia de GTA V Legacy:
un servidor propio en Windows, un cliente `.asi` en cada PC que juega y el mod **Gori Online** con el
contenido (personaje freemode, dinero, propiedades, negocios —también desguaces, oficinas de fianzas, la
fábrica textil y los Money Fronts—, golpes, carreras Transform, deathmatch, Survival, misiones de contacto,
Drug Wars, Los Santos Customs y Benny's como en Online, Arena War con su taller, Ammu-Nation, teléfono de
Online...).

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

## GodEye: la partida vista (y jugada) desde el navegador

<img src="docs/img/godeye-hack.png" alt="Minijuego de hackeo de GodEye con Zorua" width="420" align="right">

**GodEye** es una web que corre en el PC servidor junto a `LocalOnlineServer.exe`. Sirve para ver la partida en
directo y para jugar contra los que están en GTA sin tener GTA:

- **Mapa en directo:** jugadores, coches, velocidad, vida, estrellas y zona.
  - Mapa 2D.
  - Mapa 3D real, hecho en tu PC con *tus* archivos del juego, con Cayo Perico y las islas propias. Sigue la hora,
    el tiempo y los apagones de la partida.
- **Cámaras:** un segundo GTA en el PC servidor hace de cámara.
  - Tipos: CCTV, seguir, helicóptero, dron, cinemática…
  - Vídeo en directo: WebRTC en la LAN y HLS desde Internet.
- **Juego asimétrico por roles:**
  - HACKER: semáforos y apagones.
  - DISPATCH: estrellas, alertas y controles policiales.
  - OPERATOR: clima, hora, cámaras y eventos.
- **Hack Credits:**
  - Las acciones se pagan con créditos. Se ganan mirando, con un bonus diario y con recompensas.
  - Las acciones gordas piden antes un **minijuego de hackeo** que comprueba el servidor.
  - También hay límites diarios, cooldowns y eventos: tormenta eléctrica, atasco, persecución y happy hour.
- **Cuentas y panel de control:**
  - Cada uno se crea su cuenta en la web o en la app de GodEye y elige bando.
  - La primera cuenta de una instalación nueva es la del **ADMIN**, el dueño del servidor. Su **panel de control**
    muestra el PC (CPU, RAM, gráficas, discos), el servidor del juego, GodEye, las cuentas y roles, los ajustes, los
    créditos y los logs.
- **Seguridad:**
  - Login con roles, CSRF y límites por usuario.
  - Registro de todas las acciones.
  - El navegador nunca habla con GTA: todo pasa por el servidor.
- **`GodEye.exe`:** la abre y la cierra desde el escritorio, en la tarjeta gráfica que elijas.
- **Acceso desde fuera (opcional):** dirección pública con Cloudflare Tunnel.
- **Mascota:** Zorua, con sprites de la comunidad (ver créditos abajo).

Cómo montarla: [godeye/README.md](godeye/README.md) · diseño: [docs/godeye.md](docs/godeye.md).

<br clear="right">

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
godeye/              GodEye: backend (Node.js), web, lanzador GodEye.exe, Worker de Cloudflare, sprites de Zorua
tools/godeye-map/    genera los mapas 2D/3D de GodEye desde tus archivos del juego (no se publican)
docs/                guía de instalación y documentación técnica
third_party/         ENet y SQLite (ver THIRD_PARTY.md)
```

Documentación técnica: [arquitectura](docs/architecture.md), [protocolo](docs/protocol.md),
[decisiones](docs/decisions.md), [multijugador y contenido](docs/multiplayer.md), [Proton](docs/proton.md),
[problemas conocidos](docs/troubleshooting.md).

## Licencia

[MIT](LICENSE). ENet (MIT), SQLite (dominio público) y three.js (MIT) mantienen sus licencias: ver
[THIRD_PARTY.md](THIRD_PARTY.md).

Los sprites de Zorua de `godeye/` son de la comunidad de
[PMDCollab SpriteCollab](https://github.com/PMDCollab/SpriteCollab), de **baronessfaron**, **Emmuffin** y
**MilesFarber**. Su licencia es [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/): uso no comercial,
citando a sus autores. Zorua es © Nintendo / Creatures / GAME FREAK; GodEye es un proyecto de fans sin ánimo de
lucro.
GTA V y sus marcas pertenecen a Rockstar Games / Take-Two; este proyecto no está afiliado a ellos.
