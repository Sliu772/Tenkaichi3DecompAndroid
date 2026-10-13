#!/bin/sh
# The first thing that could have sunk the iOS build, answered in seconds. RESULT (CI, LLVM 21.1.8, Xcode 15.4):
#   - clang -mabi=aapcs-soft on the Darwin triple CRASHES while generating a function that DEFINES a variadic function
#     (va_start/va_arg). The game and the port define none (only calls: sprintf, fprintf, sceSdRemote), so it does not matter.
#   - the same flags compile variadic CALLS fine, and the arguments go on the stack (Apple's convention), floats are
#     lowered to __addsf3 & co. as on Android.
#   - without aapcs-soft, clang refuses any float on Darwin ('ABI darwinpcs does not support it'): the soft ABI is needed.
# This script keeps both checks so a toolchain change that breaks either shows up here.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LLVM=${BT3_LLVM:-$ROOT/port/build/ios-toolchain/llvm}
SDK=${BT3_SYSROOT:-$(xcrun --sdk iphoneos --show-sdk-path)}
T=$(mktemp -d)
cat > "$T/t.c" <<'EOC'
#include <stdio.h>
extern int vf(const char *fmt, ...);
float g(float a, float b) { return a * b + a; }
int h(char *buf, int x, int y, float z) { return sprintf(buf, "%d %d", x, y) + vf("x", y, x, z) + (int)g(1.0f, z); }
EOC
cat > "$T/def.c" <<'EOC'
#include <stdarg.h>
int f(const char *fmt, ...) { va_list ap; int v; va_start(ap, fmt); v = va_arg(ap, int); va_end(ap); return v; }
EOC
FLAGS="-mabi=aapcs-soft -mgeneral-regs-only"
echo "=============== 1) variadic CALLS + soft float (what the game does): must be accepted"
if "$LLVM/bin/clang" --target=arm64-apple-ios15.0 -isysroot "$SDK" -std=gnu89 -O2 -fno-builtin -w -fms-extensions $FLAGS -S "$T/t.c" -o "$T/t.s" 2>"$T/err"; then
    echo "accepted; the call to sprintf/vf (arguments stored at [sp] = on the stack, as Apple's libc expects):"
    awk '/^_h:/,/^Lfunc_end/' "$T/t.s" | grep -E 'stp|str |bl\s' | head -12
else
    echo "REFUSED (this breaks the build):"; head -8 "$T/err"; exit 1
fi
echo "=============== 2) a variadic DEFINITION (the game has none): known to crash clang, informational"
if "$LLVM/bin/clang" --target=arm64-apple-ios15.0 -isysroot "$SDK" -std=gnu89 -O2 -w $FLAGS -S "$T/def.c" -o "$T/def.s" 2>"$T/err2"; then
    echo "accepted (new: variadic definitions are possible)"
else
    echo "refused/crashed as before: do not define variadic functions in game or port code compiled with the soft ABI"
fi
rm -rf "$T"
