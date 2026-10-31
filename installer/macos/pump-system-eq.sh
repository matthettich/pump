#!/bin/sh
# pump: keeps a running System EQ from outliving an update. Run as root by the installer (pump-macos.pkg).
#   pump-system-eq.sh stop    before anything is installed: if System EQ is running, quit it the normal way (so it
#                             removes its audio tap and its crash guardian leaves it closed) and note that it was
#                             running; if it hasn't gone after 10 s, stop the guardian first, then the app
#   pump-system-eq.sh start   after everything is installed: open the new System EQ again if it was running before
# An old copy left running kept playing through the old code after an update, which looked like the new version.
# PUMP_TEST_ROOT=<dir> keeps the marker inside <dir> (tests).
MARK="${PUMP_TEST_ROOT:-}/private/tmp/pump-system-eq-was-running"
APP="/Applications/System EQ.app"
EXE="System EQ.app/Contents/MacOS/system-eq"

# the logged-in user (the installer runs as root): System EQ runs as them, so quit and open it as them
u=$(stat -f%Su /dev/console 2>/dev/null)
uid=""
[ -n "$u" ] && [ "$u" != root ] && uid=$(id -u "$u" 2>/dev/null)
as_user() { if [ -n "$uid" ]; then launchctl asuser "$uid" sudo -u "$u" "$@"; else "$@"; fi; }

# process ids by their executable (first word ends in the app's executable; never this script or its parents)
pids() {   # $1: app | guard | all
  ps -axo pid=,args= | while read -r pid args; do
    case "$args" in
      */"$EXE"" --guard "*) [ "$1" = app ] || echo "$pid" ;;
      */"$EXE"|*/"$EXE"" "*) [ "$1" = guard ] || echo "$pid" ;;
    esac
  done
}
app_running() { [ -n "$(pids app)" ]; }
any_running() { [ -n "$(pids all)" ]; }

case "$1" in
stop)
  rm -f "$MARK"
  any_running || exit 0
  # a guardian with no app (it waits for an app that is gone) can simply go
  app_running || { kill $(pids guard) >/dev/null 2>&1; exit 0; }
  app_running && touch "$MARK"
  # the normal quit: the app writes its clean-exit marker, so the guardian doesn't open it again
  as_user osascript -e 'tell application id "com.wake.pump.systemeq" to quit' >/dev/null 2>&1
  i=0
  while app_running && [ $i -lt 20 ]; do sleep 0.5; i=$((i + 1)); done
  if app_running; then
    kill $(pids guard) >/dev/null 2>&1   # the guardian first, or it would open the old app again
    kill $(pids app) >/dev/null 2>&1
    sleep 1
    p=$(pids all); [ -n "$p" ] && kill -9 $p >/dev/null 2>&1
  fi
  exit 0
  ;;
start)
  [ -f "$MARK" ] || exit 0
  rm -f "$MARK"
  [ -d "$APP" ] && as_user open -a "$APP" >/dev/null 2>&1
  exit 0
  ;;
esac
exit 0
