#!/bin/sh
# usage: ios_package.sh [folder of SDL3.xcframework]   ->   port/build/ios/bt3.ipa (unsigned: Sideloadly / SideStore sign it)
# Needs port/build/ios/bt3 (ios_link.py) and, for a build without the game's data, bt3.dat next to it.
# The app's Documents folder (visible in the Files app: UIFileSharingEnabled) is the game's working folder: bt3_log.txt is
# written there, and gamedata/ goes there.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${BT3_OUT:-$ROOT/port/build/ios}
XCF=${1:-${BT3_SDL3_XCF:-$ROOT/SDL3.xcframework}}
SLICE=$(find "$XCF" -maxdepth 1 -type d -name 'ios-arm64*' ! -name '*simulator*' | head -1)
[ -n "$SLICE" ] || { echo "no ios-arm64 slice in $XCF"; exit 1; }
APP=$OUT/ipa/Payload/BT3.app
rm -rf "$OUT/ipa" "$OUT/bt3.ipa"
mkdir -p "$APP/Frameworks"
cp "$OUT/bt3" "$APP/bt3"
[ -f "$OUT/bt3.dat" ] && cp "$OUT/bt3.dat" "$APP/bt3.dat"
cp "$ROOT/port/ios/Info.plist" "$APP/Info.plist"
cp -R "$SLICE/SDL3.framework" "$APP/Frameworks/"
# MoltenVK (Vulkan on Metal): the renderer's shaders are SPIR-V, which SDL_GPU only takes through its Vulkan driver.
# BT3_MOLTENVK: the folder of MoltenVK.xcframework (the "dynamic" one of MoltenVK-ios.tar).
MVK=${BT3_MOLTENVK:-}
if [ -n "$MVK" ]; then
    MSLICE=$(find "$MVK" -maxdepth 1 -type d -name 'ios-arm64*' ! -name '*simulator*' | head -1)
    [ -n "$MSLICE" ] && [ -d "$MSLICE/MoltenVK.framework" ] || { echo "no ios-arm64 MoltenVK.framework in $MVK"; exit 1; }
    cp -R "$MSLICE/MoltenVK.framework" "$APP/Frameworks/"
else
    echo "warning: BT3_MOLTENVK is not set: the .ipa has no Vulkan library and will have no renderer"
fi
(cd "$OUT/ipa" && zip -qry ../bt3.ipa Payload)
ls -la "$OUT/bt3.ipa"
