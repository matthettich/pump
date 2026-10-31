#!/bin/sh
# Packages pump CONVERT with Electron and its own ffmpeg: run on a Mac for the Mac app, on Windows (Git Bash) for
# Windows. Needs Node. Electron, the packager and the ffmpeg binaries (static builds with the SoX resampler and LAME,
# via the ffmpeg-static package) are fetched by npm.
#
#   sh convert/build.sh            -> convert/dist/
#     Mac:     pump-convert-macos.zip ("pump CONVERT.app", universal, ad-hoc signed; ffmpeg for arm64 and x64)
#     Windows: pump-convert-windows-setup.exe (installer) and pump-convert-windows.zip
# After packaging, the shipped ffmpeg is tested with convert/test.mjs (real conversions and quality checks).
set -e
H=$(cd "$(dirname "$0")/.." && pwd)
C="$H/convert"
cd "$C"
mkdir -p app/fonts
cp "$H"/common/fonts/*.ttf app/fonts/
rm -rf ffmpeg
npm install --no-audit --no-fund --save-dev electron@45.1.0 electron-builder@26.18.0
# ffmpeg for each architecture the app runs on (ffmpeg-static downloads the binary for npm_config_platform/arch)
fetch() {   # platform arch
  rm -rf node_modules/ffmpeg-static
  npm_config_platform=$1 npm_config_arch=$2 npm install --no-audit --no-fund --no-save ffmpeg-static@5.2.0
  mkdir -p "ffmpeg/$1-$2"
  B=node_modules/ffmpeg-static/ffmpeg; [ -f "$B.exe" ] && B="$B.exe"
  cp "$B" "ffmpeg/$1-$2/"
  cp node_modules/ffmpeg-static/*LICENSE* "ffmpeg/$1-$2/" 2>/dev/null || true
  cp node_modules/ffmpeg-static/*README* "ffmpeg/$1-$2/" 2>/dev/null || true
}
case "$(uname -s)" in
  Darwin)
    fetch darwin arm64
    fetch darwin x64
    HOSTFF="ffmpeg/darwin-$( [ "$(uname -m)" = arm64 ] && echo arm64 || echo x64 )/ffmpeg"
    chmod +x ffmpeg/*/ffmpeg
    PUMP_FFMPEG="$C/$HOSTFF" node test.mjs
    npx electron-builder --mac dir --universal --publish never
    APP=$(ls -d dist/mac-universal/*.app | head -1)
    codesign --force --deep -s - "$APP"
    codesign --verify --deep --strict "$APP"
    ditto -c -k --sequesterRsrc --keepParent "$APP" dist/pump-convert-macos.zip
    echo "built $C/dist/pump-convert-macos.zip"
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    fetch win32 x64
    PUMP_FFMPEG="$C/ffmpeg/win32-x64/ffmpeg.exe" node test.mjs
    npx electron-builder --win --x64 --publish never
    echo "built $C/dist/pump-convert-windows-setup.exe and pump-convert-windows.zip"
    ;;
  *)
    fetch linux x64
    chmod +x ffmpeg/linux-x64/ffmpeg
    PUMP_FFMPEG="$C/ffmpeg/linux-x64/ffmpeg" node test.mjs
    npx electron-builder --linux dir --publish never
    echo "built $C/dist/linux-unpacked (for local tests only)"
    ;;
esac
