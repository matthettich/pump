#!/bin/sh
# Packages pump ROOM as a desktop app with Electron: run on a Mac for the Mac app, on Windows (Git Bash) for Windows.
# Needs bin/pump-room.html first (make room-page) and Node. Electron and its packager are fetched by npm (pinned below).
#
#   sh room/electron/build.sh            -> room/electron/dist/
#     Mac:     pump-room-macos.zip ("pump ROOM.app", universal, ad-hoc signed)
#     Windows: pump-room-windows-setup.exe (installer) and pump-room-windows.zip
#   PUMP_ROOM_SMOKE=1 on the packaged app runs a measurement of the simulated room and quits (main.js).
set -e
H=$(cd "$(dirname "$0")/../.." && pwd)
E="$H/room/electron"
[ -f "$H/bin/pump-room.html" ] || { echo "bin/pump-room.html is missing: build it first (make room-page)"; exit 1; }
mkdir -p "$E/app"
cp "$H/bin/pump-room.html" "$E/app/pump-room.html"
cd "$E"
# pinned (the versions the first CI build used); recorded in package.json for the packager while it runs, not committed
npm install --no-audit --no-fund --save-dev electron@45.1.0 electron-builder@26.18.0
npx electron --version
npx electron-builder --version
case "$(uname -s)" in
  Darwin)
    npx electron-builder --mac dir --universal --publish never
    APP=$(ls -d dist/mac-universal/*.app | head -1)
    # ad-hoc signature, inside out (Apple Silicon refuses unsigned code; the packager's edits broke Electron's own)
    codesign --force --deep -s - "$APP"
    codesign --verify --deep --strict "$APP"
    ditto -c -k --sequesterRsrc --keepParent "$APP" dist/pump-room-macos.zip
    echo "built $E/dist/pump-room-macos.zip"
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    npx electron-builder --win --x64 --publish never
    echo "built $E/dist/pump-room-windows-setup.exe and pump-room-windows.zip"
    ;;
  *)
    npx electron-builder --linux dir --publish never
    echo "built $E/dist/linux-unpacked (for local tests only)"
    ;;
esac
