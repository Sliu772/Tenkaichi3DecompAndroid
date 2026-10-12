#!/bin/sh
# The first thing that can sink the iOS build, answered in seconds.
#
# The game's code is compiled with the soft-float ABI (-mabi=aapcs-soft -mgeneral-regs-only) so that every float operation
# goes to the PS2's own arithmetic (softfloat_ps2.c). That is fine on Android, where the C library uses AAPCS64. Apple's
# arm64 ABI (DarwinPCS) differs in one way that matters here: ARGUMENTS OF VARIADIC FUNCTIONS GO ON THE STACK, not in
# registers. The game calls sprintf() 428 times, fprintf() 12, and may define variadic functions of its own.
#
# This compiles a small file the way the game's code is compiled and shows how each case is passed. Read the result as:
#   - clang refuses the flags                       -> the soft ABI does not exist for the Darwin triple (plan B needed)
#   - sprintf's `%d` argument is stored to [sp]     -> the call matches Apple's C library (good)
#   - sprintf's `%d` argument is in x2              -> the call does NOT match libSystem: every sprintf needs a wrapper
#   - f() reads its variadic int from a register    -> game-defined variadic functions use AAPCS inside, Darwin outside
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LLVM=${BT3_LLVM:-$ROOT/port/build/ios-toolchain/llvm}
SDK=${BT3_SYSROOT:-$(xcrun --sdk iphoneos --show-sdk-path)}
T=$(mktemp -d)
cat > "$T/t.c" <<'EOC'
#include <stdio.h>
#include <stdarg.h>
int f(const char *fmt, ...) { va_list ap; int v; va_start(ap, fmt); v = va_arg(ap, int); va_end(ap); return v; }
float g(float a, float b) { return a * b + a; }
int h(char *buf, int x, int y) { return sprintf(buf, "%d", x) + f("x", y) + (int)g(1.0f, 2.0f); }
EOC
for FLAGS in "-mabi=aapcs-soft -mgeneral-regs-only" "-mgeneral-regs-only"; do
    echo "=============== clang $FLAGS"
    if "$LLVM/bin/clang" --target=arm64-apple-ios15.0 -isysroot "$SDK" -std=gnu89 -O2 -fno-builtin -w -fms-extensions $FLAGS -c "$T/t.c" -o "$T/t.o" 2>"$T/err"; then
        echo "accepted"
        "$LLVM/bin/llvm-objdump" -d --no-show-raw-insn "$T/t.o" | sed -n '1,60p'
    else
        echo "REFUSED:"; head -8 "$T/err"
    fi
done
rm -rf "$T"
