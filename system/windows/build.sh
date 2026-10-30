#!/bin/sh
# Builds bin/pump-room-profiles.exe: Room Profiles, the tray app that switches pump ROOM profiles on Windows (see
# room-profiles.cpp). Cross-compiles with MinGW, as the plugins' Windows build does. Run from the repo root:
#   sh system/windows/build.sh            (CXX / CC default to the MinGW posix compilers)
set -e
CXX="${CXX:-x86_64-w64-mingw32-g++-posix}"
CC="${CC:-x86_64-w64-mingw32-gcc-posix}"
O=$(mktemp -d)
mkdir -p bin
"$CC" -O2 -DPFFFT_STATIC_DEFINE -Icommon -c common/pump_pffft.c -o "$O/pffft.o"
"$CXX" -O2 -std=c++17 -municode -mwindows -Wall -Wno-unused-parameter -Wno-missing-field-initializers -DPFFFT_STATIC_DEFINE -Icommon \
    system/windows/room-profiles.cpp "$O/pffft.o" -o bin/pump-room-profiles.exe \
    -static -lole32 -luuid -lshell32 -ladvapi32 -lgdi32 -luser32 -lcomdlg32
rm -rf "$O"
echo "built bin/pump-room-profiles.exe"
