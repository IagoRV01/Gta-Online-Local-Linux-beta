# Guía de instalación desde cero

Esta guía monta **GTA Local Online** completo en tu casa: un servidor y uno o varios PCs jugando en la
misma red local. No hace falta saber programar: solo descargar, copiar archivos y editar dos ficheros de
texto. Calcula unos 30 minutos la primera vez.

**Índice**
1. [Qué necesitas](#1-qué-necesitas)
2. [Montar el servidor (Windows)](#2-montar-el-servidor-windows)
3. [Preparar cada PC que juega (Windows)](#3-preparar-cada-pc-que-juega-windows)
4. [Preparar un PC que juega con Linux, Steam Deck o Bazzite](#4-preparar-un-pc-que-juega-con-linux-steam-deck-o-bazzite)
5. [Primera partida](#5-primera-partida)
6. [Contenido opcional: carreras](#6-contenido-opcional-carreras)
7. [Administrar el servidor](#7-administrar-el-servidor)
8. [Actualizar](#8-actualizar)
9. [Solución de problemas](#9-solución-de-problemas)
10. [Compilar tú mismo](#10-compilar-tú-mismo)

---

## 1. Qué necesitas

| Qué | Detalles |
|---|---|
| **GTA V Legacy** | La versión clásica (Steam app 271590, Rockstar Launcher o Epic), actualizada a la **1.0.3889**. GTA V *Enhanced* no sirve. Cada jugador necesita su propia copia. |
| **ScriptHookV para 3889** | De [dev-c.com](http://www.dev-c.com/gtav/scripthookv/) (Alexander Blade). Trae `ScriptHookV.dll` y `dinput8.dll` (el cargador de `.asi`). No se incluye aquí porque su licencia no permite redistribuirlo. |
| **Un PC con Windows 10/11 para el servidor** | Puede ser uno de los que juegan. El servidor no necesita GTA ni tarjeta gráfica; gasta muy poco. |
| **Red local** | Todos los PCs en la misma red. Mejor por cable: algunas redes Wi-Fi en malla aíslan a los dispositivos entre sí. |
| **La release** | En la página [Releases](../../../releases) del repositorio: `GTALocalOnline-Server-<versión>.zip` y `GTALocalOnline-Client-<versión>.zip`. |

> **Cómo saber tu versión de GTA:** clic derecho en `GTA5.exe` → *Propiedades* → *Detalles* →
> *Versión del producto* debe ser **1.0.3889.0**. Si Rockstar saca una actualización, ScriptHookV deja
> de funcionar hasta que publiquen la suya, y estos `.asi` pueden necesitar una actualización también.

---

## 2. Montar el servidor (Windows)

1. **Descomprime** `GTALocalOnline-Server-<versión>.zip` en una carpeta fija, por ejemplo
   `C:\GTALocalOnline\`. Dentro verás:
   ```
   LocalOnlineServer.exe   el servidor
   server.example.toml     configuración de ejemplo
   start-server.bat        arranca el servidor
   setup-firewall.ps1      abre el puerto en el cortafuegos de Windows
   netprobe.exe            prueba de red
   ```
2. **Crea la configuración:** copia `server.example.toml` y renombra la copia a `server.toml`. Los valores
   por defecto sirven; los más útiles:
   ```toml
   [server]
   name = "GTA Local Online"      # nombre de tu servidor
   [network]
   game_port = 4499               # puerto UDP
   max_players = 8
   [accounts]
   allow_registration = true      # cada nombre nuevo crea su cuenta al entrar por primera vez
   [combat]
   pvp = true                     # los jugadores pueden herirse entre sí
   ```
3. **Abre el cortafuegos:** clic derecho en `setup-firewall.ps1` → *Ejecutar con PowerShell* (pedirá
   permisos de administrador). Crea una regla que permite **UDP 4499 solo desde tu red local**.
   - Si Windows bloquea el script, abre PowerShell en esa carpeta y ejecuta:
     `powershell -ExecutionPolicy Bypass -File .\setup-firewall.ps1`
   - Para quitar la regla más adelante: `Remove-NetFirewallRule -DisplayName 'GTA Local Online (UDP)'`
4. **Arranca el servidor:** doble clic en `start-server.bat`. Debe aparecer algo así:
   ```
   INFO  [net] listening on UDP 0.0.0.0:4499, tick 20 Hz, max 8 players, protocol v10
   ```
   La primera vez Windows puede preguntar si permites el acceso a la red: acepta para **redes privadas**.
   Deja la ventana abierta mientras jugáis (cerrarla apaga el servidor).
5. **Apunta la IP del servidor:** abre `cmd` y escribe `ipconfig`. Busca la *Dirección IPv4* del adaptador
   **Ethernet** (si usas cable) o Wi-Fi, por ejemplo `192.168.1.10`.
   - Recomendado: en tu router, **reserva esa IP** para este PC (DHCP estático). Si no, puede cambiar al
     reiniciar y tendrás que editar los `client.toml` otra vez.

Los datos se guardan en `data\localonline.db` (cuentas, personajes, dinero) y los registros en
`logs\game-server.log`, junto al `.exe`.

**Arranque automático (opcional):** Programador de tareas de Windows → *Crear tarea básica* → *Al iniciar
sesión* → *Iniciar un programa* → `start-server.bat` (marca *Iniciar en* con la carpeta del servidor).

---

## 3. Preparar cada PC que juega (Windows)

Busca la carpeta de GTA V (la que contiene `GTA5.exe`). En Steam: clic derecho en el juego → *Administrar*
→ *Ver archivos locales*.

1. **ScriptHookV:** descarga la versión para 3889 y copia de su carpeta `bin\` a la carpeta de GTA V:
   - `ScriptHookV.dll`
   - `dinput8.dll`
   - (no hace falta `NativeTrainer.asi`; si lo copias, su tecla F4 no molesta).
   > Windows Defender a veces borra `ScriptHookV.dll` al descomprimirlo. Si pasa, restáuralo desde
   > *Protección contra virus y amenazas → Historial de protección* o añade una exclusión para la carpeta
   > del juego. Es una decisión tuya: hazlo solo si confías en el archivo que has descargado.
2. **Los mods:** descomprime `GTALocalOnline-Client-<versión>.zip` y copia **todo** a la carpeta de GTA V:
   ```
   LocalOnline.asi
   GoriOnline.asi
   LocalOnline\client.toml
   ```
3. **Configura el cliente:** abre `LocalOnline\client.toml` con el Bloc de notas y cambia dos líneas:
   ```toml
   [server]
   address = "192.168.1.10"   # la IP del servidor del paso 2.5 (127.0.0.1 si el servidor es este mismo PC)
   port = 4499

   [player]
   name = "TuNombre"          # tu nombre para los demás: 2-24 letras, números, espacios, . - _
   ```
   El nombre es tu cuenta: la primera vez que entras el servidor la crea y guarda un *token* secreto en
   `LocalOnline\cache\`. Si cambias de PC con el mismo nombre, copia también esa carpeta `cache`.
4. Arranca GTA V normalmente desde Steam/Rockstar/Epic.

Para desinstalar: borra `LocalOnline.asi`, `GoriOnline.asi` y la carpeta `LocalOnline\` (y ScriptHookV si
no lo usas para otra cosa). Tu partida del modo historia no se toca.

---

## 4. Preparar un PC que juega con Linux, Steam Deck o Bazzite

GTA V Legacy funciona con **Proton** y los `.asi` también. Se ha probado con *Proton Experimental*.

1. Haz lo mismo que en el [paso 3](#3-preparar-cada-pc-que-juega-windows): ScriptHookV (`ScriptHookV.dll`,
   `dinput8.dll`), `LocalOnline.asi`, `GoriOnline.asi` y `LocalOnline/client.toml` en la carpeta del juego,
   normalmente `~/.local/share/Steam/steamapps/common/Grand Theft Auto V/`.
2. En Steam: clic derecho en GTA V → *Propiedades* → *General* → **Opciones de lanzamiento**:
   ```
   WINEDLLOVERRIDES="dinput8,xinput1_4=n,b" %command%
   ```
   Sin esto, Wine usa su propio `dinput8` y **ningún `.asi` se carga**.
3. En *Compatibilidad*, fuerza **Proton Experimental** (u otra versión reciente).
4. No instales ScriptHookVDotNet: bajo Wine Mono se cuelga al cargar la historia (y no hace falta).

Para copiar los archivos desde un PC Windows por SSH tienes `client/linux/deploy.sh` en el código fuente
(variables `BC250_HOST` / `BC250_IP` con el alias SSH y la IP del PC Linux).

---

## 5. Primera partida

1. Con el servidor encendido, abre GTA V. Al cargar aparece la pantalla de inicio de Local Online:
   - **STORY MODE**: el modo historia normal, sin conectarse.
   - **LOCAL ONLINE**: entra con tu personaje de Online.
2. La primera vez se abre el **creador de personaje** (aspecto, ropa, nombre). Después apareces en Los
   Santos como en Online: con tu dinero, tu rango y tu ropa, guardados en el servidor.
3. Teclas:
   | Tecla | Qué abre |
   |---|---|
   | **M** (o mantener *Back/Select* en el mando) | Menú de interacción de Gori Online: propiedades, negocios, golpes, misiones de contacto, carreras, Survival, deathmatch, vehículos, Internet... |
   | **F10** | Menú de LocalOnline: estado de la conexión, jugadores, opciones. Se cambia en `client.toml` (`menu_key`). |
   | **Flecha arriba** | Teléfono de Online (contactos, Internet, trabajos). |
4. **Jugar juntos:** cuando alguien abre un trabajo, una carrera, un deathmatch o un Survival, al resto le
   llega un aviso. Para unirse: Menú de interacción (**M**) → **Jobs**. El anfitrión lo inicia desde su
   menú cuando estáis todos; las carreras, deathmatch y Survival empiezan con cuenta atrás a la vez.
5. Los pagos de los trabajos los hace el servidor, directamente al banco de cada jugador. Las carreras pagan como
   en Online: online, según tu posición; en solitario, la mitad de lo que gana el primero (mínimo 1.000 $).

> Consejo: no te quedes dentro de un interior o bajo el agua al cerrar el juego; apareces donde se guardó
> tu posición.

---

## 6. Contenido opcional: carreras

Gori Online lee carreras en formato XML de **Community Races** (el mod de carreras de GTA5-Mods):

- Pon los `.xml` en `GoriOnline\Races\` dentro de la carpeta de GTA V (crea las carpetas si no existen).
  Aparecen en Menú de interacción → **Races**, y en el mapa como banderas de carrera.
- Para pistas sin puntos de control (mapas de *stunt* en formato Map Editor XML con `<Metadata>`), ponlas en
  `GoriOnline\Tracks\` y usa **Races → Race Creator**: recorres la pista, marcas los checkpoints con **E**,
  colocas la parrilla y la guardas como carrera.

- **Carreras Transform:** algunos checkpoints (morados) te cambian de vehículo a la misma velocidad: de quad a
  moto de agua, de moto de agua a helicóptero... El repositorio trae una de ejemplo hecha para este proyecto:
  copia `gori-online/races/Gori_Transform_Vespucci.xml` a `GoriOnline\Races\`.
- **Vehículos especiales y Hotring:** en el menú de cualquier carrera, *Vehicle Class* cambia los coches de la
  carrera por los vehículos especiales de Online (Rocket Voltic, Ruiner 2000, Blazer Aqua, Scramjet...) o por
  los Hotring.
- **Crear tus carreras en cualquier sitio:** Races → Race Creator → *New Race Here (no track)*. Marca checkpoints
  con **E**; *Transform At Next Checkpoint* elige en qué vehículo te transforma el siguiente checkpoint.

Las demás carreras no se incluyen en este repositorio: descárgalas de sus autores (por ejemplo, packs de
carreras y circuitos de GTA5-Mods) respetando sus licencias.

---

## 7. Administrar el servidor

- **Comandos en vivo:** escribe una línea en el archivo `server.cmd` (junto al `.exe`); el servidor la lee
  en menos de un segundo y lo vacía:
  ```
  time 22 30          pone la hora a las 22:30 para todos
  freeze on|off       congela el reloj
  weather RAIN        clima fijo (CLEAR, CLOUDS, RAIN, THUNDER, FOGGY, SNOW...)
  weather auto        vuelve al clima automático
  blackout on|off     apagón en toda la ciudad
  ```
- **Cerrar el registro de cuentas:** cuando todos tengáis cuenta, pon `allow_registration = false`.
- **Límites de economía** (`[economy]` en `server.toml`): máximo por transacción, máximo de ingresos por
  minuto y dinero inicial. Protegen contra mods o errores que generen dinero infinito.
- **Copias de seguridad:** la base de datos usa modo WAL, así que copiar solo `localonline.db` con el
  servidor encendido puede dar una copia vacía. Apaga el servidor y copia `data\` entera
  (`localonline.db`, `localonline.db-wal`, `localonline.db-shm`).
- **Probar la red sin GTA:** desde cualquier PC Windows de la red:
  ```
  netprobe.exe 192.168.1.10 4499 Prueba 5
  ```
  Si responde con RTT y 0 % de pérdida, el servidor es accesible. Ojo: crea una cuenta llamada "Prueba".

---

## 8. Actualizar

1. Descarga la nueva release.
2. Servidor: cierra la ventana, sustituye `LocalOnlineServer.exe` y vuelve a arrancar. `server.toml` y
   `data\` se conservan (haz copia de `data\` antes, por si acaso).
3. Clientes: con GTA cerrado, sustituye `LocalOnline.asi` y `GoriOnline.asi`.
4. Servidor y clientes deben ser de la **misma release**: si el protocolo cambia, el servidor rechaza a los
   clientes antiguos con un aviso.

---

## 9. Solución de problemas

| Síntoma | Causa y solución |
|---|---|
| No sale la pantalla *Story Mode / Local Online* | Los `.asi` no se cargan. Windows: comprueba que `dinput8.dll` y `ScriptHookV.dll` están junto a `GTA5.exe` y que la versión del juego es 1.0.3889. Linux: faltan las opciones de lanzamiento `WINEDLLOVERRIDES`. Mira `ScriptHookV.log` en la carpeta del juego. |
| ScriptHookV dice *"Unknown game version"* | Rockstar actualizó GTA V. Espera a la nueva versión de ScriptHookV (y de estos mods si hiciera falta). |
| "Server not reachable" / no conecta | 1) ¿Está abierto `start-server.bat`? 2) ¿La IP de `client.toml` es la de `ipconfig` del servidor? (cambia tras reiniciar si no la reservaste en el router). 3) ¿Ejecutaste `setup-firewall.ps1`? 4) Prueba con `netprobe.exe`. 5) Si el servidor va por Wi-Fi en una red en malla, prueba por cable. |
| Se desconecta y reconecta cada poco | Wi-Fi inestable o el PC se congela unos segundos (poca RAM). Mejor por cable. El cliente reconecta solo y vuelve a sincronizar tu progreso. |
| Linux: GTA se cierra al segundo de pulsar *Jugar* | Revisa `journalctl --user` buscando `pressure-vessel` / `capsule-capture-libs`. Si es un fallo del runtime de Steam (SteamLinuxRuntime), prueba *Verificar integridad* de *SteamLinuxRuntime_4* en la Biblioteca de Steam (Herramientas) o reinicia el PC. |
| Sin minimapa ni menú de pausa tras morir | Espera unos segundos: el modo historia termina su secuencia de muerte; está corregido en el cliente. |
| Más | [troubleshooting.md](troubleshooting.md) (en inglés). |

Los registros útiles para pedir ayuda: `LocalOnline\LocalOnline.log` y `GoriOnline.log` (carpeta del juego)
y `logs\game-server.log` (servidor).

---

## 10. Compilar tú mismo

Solo si quieres modificar el código; la release ya trae todo compilado.

1. Instala en Windows:
   - [w64devkit](https://github.com/skeeto/w64devkit/releases) (descomprime en, por ejemplo, `C:\w64devkit`).
   - [Python 3](https://www.python.org/downloads/).
   - [Git](https://git-scm.com/download/win) (trae Git Bash).
2. Clona y compila desde Git Bash (o abriendo `w64devkit.exe`):
   ```bash
   git clone <url-de-este-repositorio>
   cd gta-local-online
   export W64DEVKIT=/c/w64devkit/bin
   ./build.sh          # tests + LocalOnline.asi + LocalOnlineServer.exe + netprobe.exe
   ./build.sh gori     # GoriOnline.asi
   ```
3. Resultado en `build/`. Para el servidor, copia `build/LocalOnlineServer.exe`, `config/server.example.toml`
   y `server/windows/*` a una carpeta propia, como en el paso 2.

Opcional: si añades funciones nativas nuevas, descarga `natives.json` de
[alloc8or/gta5-nativedb-data](https://github.com/alloc8or/gta5-nativedb-data) a
`client/gta-plugin/tools/` y `gori-online/` para que el build regenere las cabeceras de nativas.
