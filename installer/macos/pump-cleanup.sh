#!/bin/sh
# pump: clears out the installed pump plugins on a Mac. Run as root: by the installer (pump-macos.pkg, before the new
# plugins go in) and by "Uninstall pump.command".
#   pump-cleanup.sh              pump-*.vst3 / .component / .clap in /Library/Audio/Plug-Ins and the logged-in user's
#                                ~/Library/Audio/Plug-Ins move to /Library/Audio/Plug-Ins/pump-previous (hosts don't
#                                look there), so renamed or removed plugins don't stay. In the installer, pump-plugins.txt
#                                next to this script lists the bundles it installs: those are left where they are in
#                                /Library (the installer overwrites them, whenever it runs this), so only the others
#                                move; copies in ~/Library always move (they would shadow the new ones)
#   pump-cleanup.sh --uninstall  the same, deleted, and the backup too
# Either way the Audio Unit cache is refreshed, so hosts see the change. PUMP_TEST_ROOT=<dir> works inside <dir>.
MODE=install
[ "$1" = "--uninstall" ] && MODE=uninstall
ROOT="${PUMP_TEST_ROOT:-}"
SYS="$ROOT/Library/Audio/Plug-Ins"
BACKUP="$SYS/pump-previous"

# the user's own plug-in folders (the installer runs as root: find who is logged in)
if [ -n "$ROOT" ]; then USERHOME="$ROOT/Users/test"
else
  u=$(stat -f%Su /dev/console 2>/dev/null)
  USERHOME=""
  [ -n "$u" ] && [ "$u" != root ] && USERHOME=$(dscl . -read "/Users/$u" NFSHomeDirectory 2>/dev/null | awk '{print $2}')
fi

LIST="$(dirname "$0")/pump-plugins.txt"
keep() { [ "$MODE" = install ] && [ -f "$LIST" ] && grep -qxF "$1" "$LIST"; }

rm -rf "$BACKUP"
n=0
for base in "$SYS" ${USERHOME:+"$USERHOME/Library/Audio/Plug-Ins"}; do
  [ -d "$base" ] || continue
  for kind in VST3 Components CLAP; do
    for b in "$base/$kind"/pump-*.vst3 "$base/$kind"/pump-*.component "$base/$kind"/pump-*.clap; do
      [ -e "$b" ] || continue
      [ "$base" = "$SYS" ] && keep "$(basename "$b")" && continue
      if [ "$MODE" = uninstall ]; then
        rm -rf "$b"
      else
        tag="$kind"; [ "$base" = "$SYS" ] || tag="$kind (user)"
        mkdir -p "$BACKUP/$tag" && mv "$b" "$BACKUP/$tag/"
      fi
      n=$((n + 1))
    done
  done
done
[ "$MODE" = uninstall ] && rm -rf "$BACKUP"
if [ "$MODE" = uninstall ]; then echo "Removed $n pump plugin files."
elif [ "$n" -gt 0 ]; then echo "Moved $n older pump plugin files to $BACKUP."
fi

# hosts read Audio Units from a cache: restarting its service makes them look again
[ -z "$ROOT" ] && killall -9 AudioComponentRegistrar >/dev/null 2>&1
exit 0
