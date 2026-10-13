#!/bin/sh
# Mach-O arm64 half of the p32rebase test (runs on Linux with LLVM 20+, or on the macOS runner): the rebased IR has to
# assemble and LINK (no 32-bit absolute relocation of an address above 4 GB), and the linked data tables must hold
# `symbol - header`. Also prints the load/store instructions, which should use the [xHdr, wOff, uxtw] form.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); TOOLS="$HERE/../../tools"
LLVM=${BT3_LLVM:?set BT3_LLVM}
LLD=${BT3_LLD:-$LLVM/bin/ld64.lld}
T=$(mktemp -d)
cp "$HERE/t.c" "$T/t.c"
cat >> "$T/t.c" <<'EOC'
int (* __ptr32 __uptr gFnStatic)(int) = dbl;      /* text symbol in a data table */
char gBuf[64];
char * __ptr32 __uptr gBufP = gBuf + 8;           /* symbol + offset */
EOC
"$LLVM/bin/clang" --target=arm64-apple-ios15.0 -fms-extensions -O2 -w -mabi=aapcs-soft -mgeneral-regs-only -S -emit-llvm "$T/t.c" -o "$T/t.ll"
python3 - "$T/t.ll" "$T/t2.ll" "$TOOLS" <<'PY'
import sys; sys.path.insert(0, sys.argv[3])
import irfix
t, n = irfix.fix(open(sys.argv[1]).read(), stores=True, anchor="_mh_execute_header")
open(sys.argv[2], "w").write(t); print(n, "fixes")
PY
"$LLVM/bin/llc" -O2 -filetype=obj -relocation-model=pic "$T/t2.ll" -o "$T/t.o"
"$LLVM/bin/llc" -O2 -filetype=asm -relocation-model=pic "$T/t2.ll" -o "$T/t.s"
echo "--- sum() as arm64 (the loop should load through [x, w, uxtw]):"
awk '/^_sum:/,/\.cfi_endproc/' "$T/t.s" | grep -vE '^\s*\.' | head -20
printf 'void main(void){}\nint portSum(void *p, int n){return 0;}\n' > "$T/m.c"; "$LLVM/bin/clang" --target=arm64-apple-ios15.0 -c "$T/m.c" -o "$T/m.o" -w
"$LLD" -arch arm64 -platform_version ios 15.0 17.5 -e _main -o "$T/out" "$T/t.o" "$T/m.o"
OBJDUMP=$LLVM/bin/llvm-objdump; [ -x "$OBJDUMP" ] || OBJDUMP=llvm-objdump
SYMS=$("$OBJDUMP" --syms "$T/out")
addr() { echo "$SYMS" | awk -v s="$1" '$NF==s {print "0x"$1}'; }
HDR=$(addr __mh_execute_header)
echo "--- linked data tables:"
"$OBJDUMP" -s --section=__data "$T/out" | tail -n +3
for s in _n1 _n2 _dbl _gFnStatic _gBuf _gBufP; do echo "$s = $(addr $s)  (offset from header: $(printf '0x%x' $(( $(addr $s) - HDR ))))"; done
echo "OK: linked; compare the table words with the offsets above"
