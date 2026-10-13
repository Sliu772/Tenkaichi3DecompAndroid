#!/bin/sh
# Logic test of p32rebase.py on x86-64 Linux (needs LLVM 20+: BT3_LLVM=<dir with bin/clang, bin/llc>, and gcc/cc to link).
set -e
HERE=$(cd "$(dirname "$0")" && pwd); TOOLS="$HERE/../../tools"
LLVM=${BT3_LLVM:?set BT3_LLVM}
T=$(mktemp -d)
"$LLVM/bin/clang" -target x86_64-linux-gnu -fms-extensions -O1 -w -S -emit-llvm "$HERE/t.c" -o "$T/t.ll"
python3 - "$T/t.ll" "$T/t2.ll" "$TOOLS" <<'PY'
import sys; sys.path.insert(0, sys.argv[3])
import irfix
t = open(sys.argv[1]).read()
# the anchor lives in the same section as the data so the assembler can resolve `.long sym - bt3_hdr`
i = t.index("%struct.N = type")
t = t[:i] + "@bt3_hdr = global i32 1, align 4096\n" + t[i:]
t, n = irfix.fix(t, stores=True, anchor="bt3_hdr")
open(sys.argv[2], "w").write(t); print(n, "fixes")
PY
"$LLVM/bin/llc" -O2 -filetype=obj -relocation-model=static "$T/t2.ll" -o "$T/t.o"
cc -no-pie -Wl,-Tdata=0x200000 -Wl,-Tbss=0x300000 -O1 "$HERE/main.c" "$T/t.o" -o "$T/test"
"$T/test" || { echo "kept $T"; exit 1; }
