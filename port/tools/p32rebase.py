#!/usr/bin/env python3
"""iOS: the game's 4-byte pointers (address space 271) as OFFSETS from the image's Mach-O header instead of real
addresses below 4 GB. iOS reserves the whole low 4 GB (__PAGEZERO), so `ptr32 = zero-extended address` cannot work.

    ptr32 value v       <->   real address  &_mh_execute_header + v      (v = low 32 bits of (address - header))

Everything the game keeps (structs, globals, tables) stays 4 bytes per pointer. What changes is the conversion between
a 4-byte and an ordinary pointer, which LLVM does as a plain zero extension. This pass (text in, text out, after
irfix.fix) makes the conversion explicit:

    load/store/atomic through `ptr addrspace(271) P`       -> through  getelementptr i8, @hdr, zext(P)
    `addrspacecast ptr addrspace(271) P to ptr`            -> the same
    `addrspacecast ptr P to ptr addrspace(271)`            -> trunc(P - @hdr)
    constant `addrspacecast (ptr @sym to ptr addrspace(271))` -> trunc(@sym - @hdr)  (a 32-bit absolute relocation of a
                                                               symbol above 4 GB does not exist; a difference does)
On arm64 the first form is one instruction: ldr w, [x_hdr, w_off, uxtw]. The header's address is a link-time constant
(adrp+add), so there is no base variable to load. The game's memory (heap, stack, the PS2 address ranges it uses as
integers) must lie in [hdr, hdr + 4 GB): plat_mem.c reserves it there.
"""
import re

OPQ = "ptr addrspace(271)"
LOADLIKE = re.compile(r'^\s*(?:%[\w.$-]+ = )?(?:load|atomicrmw|cmpxchg)\b')
STORE = re.compile(r'^\s*store\b')


def balanced_end(s, j):
    """Index just after the balanced `( ... )` that starts at s[j]."""
    d = 0
    while True:
        d += s[j] == "("
        d -= s[j] == ")"
        j += 1
        if d == 0:
            return j


def operand_end(s, j):
    """End of an operand that starts at s[j]: the next top-level ',' (or the end of the line)."""
    d = 0
    while j < len(s):
        c = s[j]
        if c in "([{<":
            d += 1
        elif c in ")]}>":
            d -= 1
        elif d == 0 and c == ",":
            break
        j += 1
    return j


def rel(x, anchor):
    """The 32-bit offset of the (constant) ordinary pointer expression x from the anchor."""
    return (f"trunc (i64 sub (i64 ptrtoint (ptr {x} to i64), i64 ptrtoint (ptr @{anchor} to i64)) to i32)")


CASTC = "addrspacecast (ptr "
TOP = " to ptr addrspace(271))"


def fold_consts(line, anchor):
    """`addrspacecast (ptr X to ptr addrspace(271))` -> `inttoptr (i32 <offset of X> to ptr addrspace(271))`."""
    out, pos = [], 0
    while True:
        k = line.find(CASTC, pos)
        if k < 0:
            break
        end = balanced_end(line, k + len("addrspacecast "))
        inner = line[k + len(CASTC):end - 1]           # "X to ptr addrspace(271)"
        assert inner.endswith(" to ptr addrspace(271)"), inner
        x = inner[:-len(" to ptr addrspace(271)")]
        out.append(line[pos:k] + f"inttoptr (i32 {rel(x, anchor)} to ptr addrspace(271))")
        pos = end
    return "".join(out) + line[pos:]


class Ctx:
    def __init__(self, anchor):
        self.anchor, self.n = anchor, 0

    def tmp(self):
        self.n += 1
        return f"%rb{self.n}"

    def to_ptr(self, p32, pre):
        """Emit the instructions that turn the 4-byte pointer p32 (an operand) into an ordinary pointer; returns its name."""
        i, z, q = self.tmp(), self.tmp(), self.tmp()
        pre.append(f"  {i} = ptrtoint {OPQ} {p32} to i32")
        pre.append(f"  {z} = zext i32 {i} to i64")
        pre.append(f"  {q} = getelementptr i8, ptr @{self.anchor}, i64 {z}")
        return q

    def to_p32(self, p64, pre):
        a, b, d, t = self.tmp(), self.tmp(), self.tmp(), self.tmp()
        pre.append(f"  {a} = ptrtoint ptr {p64} to i64")
        pre.append(f"  {b} = ptrtoint ptr @{self.anchor} to i64")
        pre.append(f"  {d} = sub i64 {a}, {b}")
        pre.append(f"  {t} = trunc i64 {d} to i32")
        return t


CAST_TO_PTR = re.compile(r'^(\s*%[\w.$-]+ = )addrspacecast ptr addrspace\(271\) (.*) to ptr$')
CAST_TO_P32 = re.compile(r'^(\s*%[\w.$-]+ = )addrspacecast ptr (.*) to ptr addrspace\(271\)$')


def rebase(text, anchor):
    cx = Ctx(anchor)
    out = []
    in_fn = False
    for line in text.split("\n"):
        if line.startswith("define"):
            in_fn = True
        elif line.startswith("}"):
            in_fn = False
        if not in_fn or OPQ not in line and "addrspace(271)" not in line:
            out.append(line)
            continue
        if CASTC in line:
            line = fold_consts(line, anchor)
        pre = []
        m = CAST_TO_PTR.match(line)
        if m:
            q = cx.to_ptr(m.group(2), pre)
            out.extend(pre)
            out.append(f"{m.group(1)}getelementptr i8, ptr {q}, i64 0")  # keeps the original result name
            continue
        m = CAST_TO_P32.match(line)
        if m:
            t = cx.to_p32(m.group(2), pre)
            out.extend(pre)
            out.append(f"{m.group(1)}inttoptr i32 {t} to ptr addrspace(271)")
            continue
        if STORE.match(line) or LOADLIKE.match(line):
            # the pointer operand: the last `, ptr addrspace(271) P` of a store/load, the first typed one of atomics
            if STORE.match(line) or line.lstrip().startswith("%") and " = load " in line:
                k = -1
                d = 0
                for i, c in enumerate(line):
                    if c in "([{<":
                        d += 1
                    elif c in ")]}>":
                        d -= 1
                    elif d == 0 and line.startswith(", " + OPQ + " ", i):
                        k = i
                if k >= 0:
                    s = k + 2 + len(OPQ) + 1
                    e = operand_end(line, s)
                    q = cx.to_ptr(line[s:e], pre)
                    line = line[:k + 2] + f"ptr {q}" + line[e:]
            else:  # atomicrmw / cmpxchg: `... atomicrmw op ptr addrspace(271) P,` / `cmpxchg ptr addrspace(271) P,`
                mm = re.search(r'(?:atomicrmw (?:volatile )?\w+|cmpxchg (?:weak )?(?:volatile )?) ?ptr addrspace\(271\) ', line)
                if mm:
                    s = mm.end()
                    e = operand_end(line, s)
                    q = cx.to_ptr(line[s:e], pre)
                    line = line[:mm.end() - len(OPQ) - 1] + f"ptr {q}" + line[e:]
            out.extend(pre)
        out.append(line)
    text = "\n".join(out)
    if f"@{anchor} =" not in text:
        text += f"\n@{anchor} = external hidden global i8, align 4\n"
    return text
