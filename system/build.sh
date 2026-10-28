#!/bin/sh
# Builds "bin/System EQ.app" (macOS 14.2+, universal): the system-wide pump EQ.
# Needs bin/pump-eq.component first (make eq MACOS=true), and bin/pump-room.component for Room Correction (make room
# MACOS=true; without it the app has no Room Correction). Run from the repo root:  sh system/build.sh [version]
set -e

VERSION="${1:-0.1.0}"
APP="bin/System EQ.app"
ARCHS="${PUMP_ARCHS:--arch arm64 -arch x86_64}"

[ -d bin/pump-eq.component ] || { echo "bin/pump-eq.component is missing: build the EQ first (make eq)"; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/PlugIns" "$APP/Contents/Resources"

clang++ $ARCHS -mmacosx-version-min=14.2 -std=c++17 -fobjc-arc -O2 -Wall -Wno-unused-parameter \
    system/main.mm -o "$APP/Contents/MacOS/system-eq" \
    -framework Cocoa -framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework ServiceManagement

sed "s/@VERSION@/$VERSION/g" system/Info.plist > "$APP/Contents/Info.plist"
cp -R bin/pump-eq.component "$APP/Contents/PlugIns/"
[ -d bin/pump-room.component ] && cp -R bin/pump-room.component "$APP/Contents/PlugIns/"
cp THIRD_PARTY_NOTICES.md "$APP/Contents/Resources/"

# ad-hoc signature, inside out (the bundled EQ first)
codesign --force -s - "$APP/Contents/PlugIns/pump-eq.component"
[ -d "$APP/Contents/PlugIns/pump-room.component" ] && codesign --force -s - "$APP/Contents/PlugIns/pump-room.component"
codesign --force -s - "$APP"
codesign --verify --deep --strict "$APP"
echo "built $APP"
