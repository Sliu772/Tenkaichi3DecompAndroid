#!/usr/bin/env python3
"""Cross-object check of function signatures in the final LLVM IR (iOS, BT3_KEEP=1 leaves the .ll files).

On iOS a 4-byte pointer is an OFFSET from the image header, an ordinary pointer is a real address (p32rebase.py).
ptr32.py keeps the outermost pointer of a parameter / return type ordinary, so a function defined in one object and
called from another agree on it. They disagree when a parameter is a TYPEDEF of a pointer (`McFlowCb cb`): no star
to keep, so the definition takes ptr addrspace(271) while a caller that declares `void *cb` passes a real address.
Clang cannot see this (separate objects). It crashed BootCard (McFlow_SetDoneCb). On Android the zero-extended
address made the two forms identical, which hid it.

usage: abi_sig_check.py FILE.ll...   -> one line per function whose signatures differ in pointer-ness.
"""
import re, sys, collections

HEAD = re.compile(r'^(define|declare)\b(.*?)@([\w.$]+)\(')


def split_params(s):
    out, d, cur = [], 0, ""
    for c in s:
        if c in "([{<":
            d += 1
        elif c in ")]}>":
            d -= 1
        if c == "," and d == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def kind(t):
    t = t.strip()
    if t.startswith("ptr addrspace(271)"):
        return "p32"
    if t.startswith("ptr"):
        return "ptr"
    return t.split()[0] if t else "?"


def signature(line):
    m = HEAD.match(line)
    if not m or " internal " in " " + m.group(2) + " " or " private " in " " + m.group(2) + " ":
        return None
    j = m.end() - 1
    d = 0
    while True:
        d += line[j] == "("
        d -= line[j] == ")"
        j += 1
        if d == 0:
            break
    params = split_params(line[m.end():j - 1])
    if any(p == "..." for p in params):
        return None
    before = re.sub(r"!\w+ !\d+", "", m.group(2)).strip()      # (`declare !dbg !54 ptr @f(`)
    ret = "p32" if before.endswith("addrspace(271)") else kind(before.split()[-1]) if before else "?"
    return m.group(3), (ret, tuple(kind(p) for p in params))


def main(files):
    sigs = collections.defaultdict(list)          # name -> [(file, 'define'|'declare', sig)]
    for f in files:
        for line in open(f, errors="replace"):
            if line.startswith(("define", "declare")):
                r = signature(line)
                if r:
                    sigs[r[0]].append((f.rsplit("/", 1)[-1], line.split()[0], r[1]))
    bad = 0
    for name, lst in sorted(sigs.items()):
        defs = [x for x in lst if x[1] == "define"]
        if not defs:
            continue
        d = defs[0][2]
        for fname, what, s in lst:
            if what != "declare" or len(s[1]) != len(d[1]):
                continue
            diffs = [(i, a, b) for i, (a, b) in enumerate(zip(d[1], s[1])) if a != b and ({a, b} & {"p32", "ptr"})]
            if d[0] != s[0] and {d[0], s[0]} & {"p32", "ptr"}:
                diffs.insert(0, ("ret", d[0], s[0]))
            if diffs:
                bad += 1
                print(f"{name}: defined in {defs[0][0]} as {d[1]}->{d[0]}, declared in {fname} as {s[1]}->{s[0]}; differs at "
                      + ", ".join(f"{i}: {a} vs {b}" for i, a, b in diffs))
    print(f"# {bad} mismatching declarations", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
