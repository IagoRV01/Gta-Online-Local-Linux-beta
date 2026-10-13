#!/bin/sh
# Deploys the GTA client plugin to the BC-250 over SSH (run from the Windows dev PC, MSYS2/Git bash).
#   client/linux/deploy.sh             copy LocalOnline.asi (old copy kept as .bak-<date>); client.toml only if missing
#   client/linux/deploy.sh --config    also overwrite client.toml (old one kept as .bak-<date>)
#   client/linux/deploy.sh --rollback  restore the newest LocalOnline.asi.bak-* and stop
#   client/linux/deploy.sh --remove    disable the plugin (renames it to LocalOnline.asi.disabled)
# GTA only loads .asi files at startup: a running game keeps the old version until it restarts.
# Env: BC250_HOST (ssh alias, default pc2), BC250_IP (default 192.168.1.20), GTA_DIR (relative to ~).
set -e
cd "$(dirname "$0")/../.."
HOST="${BC250_HOST:-pc2}"
IP="${BC250_IP:-192.168.1.20}"
SSH="ssh -o HostName=$IP $HOST"
SCP="scp -q -o HostName=$IP"
GAME="${GTA_DIR:-.local/share/Steam/steamapps/common/Grand Theft Auto V}"
STAMP=$(date +%Y%m%d-%H%M%S)

case "$1" in
  --rollback)
    $SSH "cd \"$GAME\" && b=\$(ls -t LocalOnline.asi.bak-* 2>/dev/null | head -1) && [ -n \"\$b\" ] && cp \"\$b\" LocalOnline.asi && echo restored \$b || echo 'no backup found'"
    exit 0 ;;
  --remove)
    $SSH "cd \"$GAME\" && [ -f LocalOnline.asi ] && mv LocalOnline.asi LocalOnline.asi.disabled && echo disabled || echo 'not installed'"
    exit 0 ;;
esac

[ -f build/LocalOnline.asi ] || { echo "build first: ./build.sh client"; exit 1; }
$SSH "test -f \"$GAME/ScriptHookV.dll\"" || { echo "ScriptHookV.dll not found in $GAME on $HOST"; exit 1; }
$SSH "mkdir -p \"$GAME/LocalOnline\" && cd \"$GAME\" && { [ -f LocalOnline.asi ] && cp LocalOnline.asi LocalOnline.asi.bak-$STAMP || true; }"
# Copy to a temp name, then rename: a rename gives the file a new inode, so a running GTA keeps
# its mapped copy of the old .asi intact (overwriting in place could corrupt the live process).
$SCP build/LocalOnline.asi "$HOST:$GAME/LocalOnline.asi.new"
$SSH "cd \"$GAME\" && mv -f LocalOnline.asi.new LocalOnline.asi"
if [ "$1" = --config ] || ! $SSH "test -f \"$GAME/LocalOnline/client.toml\""; then
  $SSH "cd \"$GAME/LocalOnline\" && { [ -f client.toml ] && cp client.toml client.toml.bak-$STAMP || true; }"
  $SCP config/client.example.toml "$HOST:$GAME/LocalOnline/client.toml.new"
  $SSH "mv -f \"$GAME/LocalOnline/client.toml.new\" \"$GAME/LocalOnline/client.toml\""
  echo "[deploy] client.toml written"
fi
# keep only the 5 newest plugin backups
$SSH "cd \"$GAME\" && ls -t LocalOnline.asi.bak-* 2>/dev/null | tail -n +6 | xargs -r rm -f"
RUNNING=$($SSH 'pgrep -f "[G]TA5.exe" >/dev/null && echo yes || echo no')
echo "[deploy] LocalOnline.asi copied ($(wc -c < build/LocalOnline.asi) bytes). GTA running: $RUNNING$( [ $RUNNING = yes ] && echo ' (restart GTA to load it)')"
