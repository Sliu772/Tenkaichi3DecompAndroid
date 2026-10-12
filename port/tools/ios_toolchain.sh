#!/bin/sh
# The iOS counterpart of android_toolchain.sh. Run on macOS (Apple silicon) with Xcode installed.
# Sets up what port/tools/android.py needs when BT3_PLATFORM=ios, in port/build/ios-toolchain (or $BT3_TOOLCHAIN):
#   llvm/            upstream LLVM 21 for macOS (clang, llc, lld, libclang.dylib). Apple's own clang cannot be used: the game's
#                    4-byte pointers (__ptr32 on AArch64, ptr32.py) and the soft-float ABI need upstream clang 20+.
#   clang-bindings/  libclang's Python module, for port/tools/ptr32.py
# The iPhoneOS SDK comes from Xcode:   export BT3_SYSROOT=$(xcrun --sdk iphoneos --show-sdk-path)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TC=${BT3_TOOLCHAIN:-$ROOT/port/build/ios-toolchain}
LLVM_VERSION=${BT3_LLVM_VERSION:-21.1.0}
mkdir -p "$TC"
cd "$TC"

if [ ! -x llvm/bin/clang ]; then
    echo "== LLVM $LLVM_VERSION for macOS"
    URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/LLVM-$LLVM_VERSION-macOS-ARM64.tar.xz"
    if curl -fsSL "$URL" -o llvm.tar.xz; then
        mkdir llvm_x && tar -xJf llvm.tar.xz -C llvm_x --strip-components=1 && rm -f llvm.tar.xz
        rm -rf llvm && mv llvm_x llvm
    else
        echo "release asset not found ($URL); trying Homebrew's llvm@21"
        brew install llvm@21
        ln -sfn "$(brew --prefix llvm@21)" llvm
    fi
fi
if [ ! -f clang-bindings/clang/cindex.py ]; then
    echo "== libclang's Python module"
    curl -fsSL "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/clang-$LLVM_VERSION.src.tar.xz" \
        | xz -dc | tar -x "clang-$LLVM_VERSION.src/bindings/python"
    rm -rf clang-bindings && mv "clang-$LLVM_VERSION.src/bindings/python" clang-bindings && rm -rf "clang-$LLVM_VERSION.src"
fi
echo "toolchain ready in $TC"
"$TC/llvm/bin/clang" --version | head -1
ls "$TC/llvm/lib/libclang.dylib"
