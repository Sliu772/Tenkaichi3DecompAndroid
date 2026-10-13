#!/bin/sh
# usage: build.sh <name> [extra linker flags]  -> port/build/probe/<name>.ipa (unsigned; Sideloadly / SideStore sign it)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
NAME=$1; shift
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
D="$ROOT/port/build/probe/$NAME"; rm -rf "$D"; mkdir -p "$D/Payload/Probe.app"
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=15.0 -O1 -fobjc-arc \
    -framework UIKit -framework Foundation -framework CoreGraphics "$@" "$ROOT/port/probe/probe.m" -o "$D/Payload/Probe.app/Probe"
cp "$ROOT/port/probe/Info.plist" "$D/Payload/Probe.app/Info.plist"
otool -l "$D/Payload/Probe.app/Probe" | grep -A4 __PAGEZERO | head -6 || true
(cd "$D" && zip -qr "../$NAME.ipa" Payload)
echo "built $ROOT/port/build/probe/$NAME.ipa"
