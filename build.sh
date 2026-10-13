#!/bin/sh
# Builds GTA Local Online with the portable w64devkit toolchain (g++ 16, C++20, static runtime).
#   ./build.sh            tests + client plugin + server
#   ./build.sh test       host unit tests only
#   ./build.sh client     tests + LocalOnline.asi        (GTA client, runs under Proton on the BC-250)
#   ./build.sh server     tests + LocalOnlineServer.exe   (Windows game server)
#   ./build.sh gori       GoriOnline.asi                  (GTA Online content mod; sources in $GORI_DIR)
# Outputs go to build/. Toolchain: $W64DEVKIT, ../gta-mods/tools/w64devkit, or a MinGW g++ on PATH.
set -e
cd "$(dirname "$0")"
TC="${W64DEVKIT:-$(cd ../gta-mods/tools/w64devkit/bin 2>/dev/null && pwd)}"
[ -n "$TC" ] || TC="$(dirname "$(command -v g++ 2>/dev/null)")"
[ -x "$TC/g++" ] || [ -x "$TC/g++.exe" ] || { echo "MinGW g++ not found (install w64devkit and set W64DEVKIT=/path/to/w64devkit/bin)"; exit 1; }
GORI_DIR="${GORI_DIR:-$( [ -d gori-online ] && echo gori-online || echo ../gta-mods/GoriOnlineNative )}"
if [ "${1:-}" = gori ]; then
  export PATH="$TC:$PATH"
  mkdir -p build
  (cd "$GORI_DIR" && { [ ! -f natives.json ] || python tools/gen_natives.py; })
  echo "[gori] GoriOnline.asi"
  "$TC/g++" -std=c++20 -O2 -shared -static -s -o build/GoriOnline.asi "$GORI_DIR/GoriOnline.cpp"
  ls -l build/GoriOnline.asi | awk '{print "[gori] ok, " $5 " bytes"}'
  exit 0
fi
export PATH="$TC:$PATH"
CXX="$TC/g++"; CC="$TC/gcc"
WHAT="${1:-all}"
mkdir -p build/obj

COMMON="shared/common/json.cpp shared/common/log.cpp shared/common/toml.cpp shared/common/character.cpp shared/common/sha256.cpp shared/common/interp.cpp shared/common/world.cpp shared/common/vehicle.cpp"
INC="-Ishared"

echo "[test] host unit tests"
"$CXX" -std=c++20 -O1 -g -Wall -Wextra $INC -o build/tests.exe tests/test_main.cpp $COMMON
./build/tests.exe
# storage tests need SQLite + the embedded schema (also used by the server build below)
[ build/obj/sqlite3.o -nt third_party/sqlite/sqlite3.c ] || "$CC" -O2 -c -DSQLITE_THREADSAFE=1 -DSQLITE_DEFAULT_FOREIGN_KEYS=1 -o build/obj/sqlite3.o third_party/sqlite/sqlite3.c
mkdir -p build/gen
python tools/embed_sql.py server/database/schema.sql build/gen/schema_sql.h
"$CXX" -std=c++20 -O1 -g -Wall -Wextra $INC -Iserver/game-server/src -Ithird_party/sqlite -Ibuild/gen -o build/storage_tests.exe   tests/test_storage.cpp server/game-server/src/storage_sqlite.cpp shared/common/log.cpp build/obj/sqlite3.o
./build/storage_tests.exe
[ "$WHAT" = test ] && exit 0

if [ "$WHAT" = all ] || [ "$WHAT" = client ]; then
  python client/gta-plugin/tools/gen_natives.py
  for f in callbacks compress host list packet peer protocol win32; do
    [ build/obj/enet_$f.o -nt third_party/enet/$f.c ] || "$CC" -O2 -c -Ithird_party/enet/include -o build/obj/enet_$f.o third_party/enet/$f.c
  done
  SRC="$(find client/gta-plugin/src -name '*.cpp' | sort) $COMMON"
  FLAGS="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type $INC -Iclient/gta-plugin/src -Ithird_party/enet/include -shared -static -static-libgcc -static-libstdc++"
  echo "[client] LocalOnline.asi"
  "$CXX" $FLAGS -g -o build/LocalOnline.dbg.asi $SRC build/obj/enet_*.o -lws2_32 -lwinmm -lshell32
  "$TC/strip" -o build/LocalOnline.asi build/LocalOnline.dbg.asi
  ls -l build/LocalOnline.asi | awk '{print "[client] ok, " $5 " bytes"}'
fi

if [ "$WHAT" = all ] || [ "$WHAT" = server ]; then
  if [ -d server/game-server/src ] && [ -n "$(ls server/game-server/src/*.cpp 2>/dev/null)" ]; then
    # third-party C code is compiled once into objects (sqlite3.c is big)
    [ build/obj/sqlite3.o -nt third_party/sqlite/sqlite3.c ] || "$CC" -O2 -c -DSQLITE_THREADSAFE=1 -DSQLITE_DEFAULT_FOREIGN_KEYS=1 -o build/obj/sqlite3.o third_party/sqlite/sqlite3.c
    for f in callbacks compress host list packet peer protocol win32; do
      [ build/obj/enet_$f.o -nt third_party/enet/$f.c ] || "$CC" -O2 -c -Ithird_party/enet/include -o build/obj/enet_$f.o third_party/enet/$f.c
    done
    mkdir -p build/gen
    python tools/embed_sql.py server/database/schema.sql build/gen/schema_sql.h
    echo "[server] LocalOnlineServer.exe"
    "$CXX" -std=c++20 -O2 -Wall -Wextra $INC -Ithird_party/enet/include -Ithird_party/sqlite -Iserver/game-server/src -Ibuild/gen \
      -static -o build/LocalOnlineServer.exe server/game-server/src/*.cpp $COMMON build/obj/*.o -lws2_32 -lwinmm
    ls -l build/LocalOnlineServer.exe | awk '{print "[server] ok, " $5 " bytes"}'
    "$CXX" -std=c++20 -O2 $INC -Ithird_party/enet/include -DNETPROBE_ECONOMY -static -o build/netprobe.exe tools/netprobe/netprobe.cpp shared/common/character.cpp shared/common/json.cpp shared/common/vehicle.cpp build/obj/enet_*.o -lws2_32 -lwinmm
    echo "[probe] netprobe.exe ok"
  else
    echo "[server] no sources yet (Phase 2)"
  fi
fi
