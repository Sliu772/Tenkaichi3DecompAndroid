#!/usr/bin/env python3
"""iOS: links the engine's objects (port/build/ios/obj, obj_data, from `android.py compile` with BT3_PLATFORM=ios) into
the Mach-O executable port/build/ios/bt3, and writes next to it the list of where the game's data tables get their values
on the user's disc (bt3.dat, the counterpart of make_dat.py / android.py finish()).

Like the Android and PC links it is done twice: the first pass asks the linker what nobody provides, a generated stub
for each of those symbols prints its name and stops the program, the second pass links with the stubs. port/build/ios/
stubs.txt lists them: that list is the work that is left.

What replaces the GNU linker's options of the other platforms (ld64 has none of them):
    --wrap=main            src/main.c is compiled with -Dmain=__real_main (android.py), the real main() is port/src/plat_ios.c
    --wrap=Progress_Main   src/main.c calls __wrap_Progress_Main directly and src/menu/menu_a_b.c defines __real_Progress_Main
    --defsym=D_3BE71C      NOT done yet (see docs/port/ios.md): a stub stands in, the game heap does not work before it exists

Environment:  BT3_SYSROOT   the iPhoneOS SDK (default: xcrun --sdk iphoneos --show-sdk-path)
              BT3_SDL3_XCF  folder of SDL3.xcframework (default: SDL3.xcframework in the repository's root)
              BT3_IOS_MIN   deployment target (default 15.0)
Usage: python3 port/tools/ios_link.py            -> port/build/ios/bt3 (+ bt3.dat, bt3.map, stubs.txt)"""
import os, pathlib, re, struct, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = pathlib.Path(os.environ.get("BT3_OUT", str(ROOT / "port/build/ios")))
OBJ, DATA, GEN = OUT / "obj", OUT / "obj_data", OUT / "gen"
EXE = OUT / "bt3"
IOS_MIN = os.environ.get("BT3_IOS_MIN", "15.0")
HDR = "__mh_execute_header"


def sh(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)


def sysroot():
    return os.environ.get("BT3_SYSROOT") or sh(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"]).stdout.strip()


def sdl3_slice():
    """The device slice of SDL3.xcframework (the folder that holds SDL3.framework)."""
    xcf = pathlib.Path(os.environ.get("BT3_SDL3_XCF", str(ROOT / "SDL3.xcframework")))
    for d in sorted(xcf.glob("ios-arm64*")):
        if "simulator" not in d.name and (d / "SDL3.framework").is_dir():
            return d
    raise SystemExit(f"no ios-arm64 slice with SDL3.framework in {xcf} (BT3_SDL3_XCF)")


def target():
    return ["-arch", "arm64", "-isysroot", sysroot(), f"-miphoneos-version-min={IOS_MIN}"]


def link_cmd(objs, extra):
    return (["xcrun", "--sdk", "iphoneos", "clang++"] + target() + ["-o", EXE] + extra + objs +
            [f"-F{sdl3_slice()}", "-framework", "SDL3", "-lm",
             "-Wl,-rpath,@executable_path/Frameworks", f"-Wl,-map,{EXE}.map"])


def undefined(stderr):
    """C names (without ld64's leading underscore) of the symbols the linker did not find."""
    names = set()
    for m in re.finditer(r'^\s+"(_[^"]+)", referenced from:', stderr, re.M):
        if re.fullmatch(r"_[A-Za-z0-9_$]+", m.group(1)):
            names.add(m.group(1)[1:])
    return sorted(names)


def symbols():
    """name (no underscore) -> offset from the image's header, for every symbol of the executable (nm -n)."""
    out = sh(["xcrun", "nm", "-n", EXE]).stdout
    syms, hdr = {}, None
    for l in out.splitlines():
        p = l.split()
        if len(p) == 3 and re.fullmatch(r"[0-9a-fA-F]{8,16}", p[0]):
            if p[2] == HDR:
                hdr = int(p[0], 16)
            syms[p[2][1:] if p[2].startswith("_") else p[2]] = int(p[0], 16)
    if hdr is None:
        raise SystemExit(f"{HDR} not found in the symbol table of {EXE}")
    return {k: v - hdr for k, v in syms.items()}


def write_data_list():
    """bt3.dat: for each run of the game's data: where it is in the program (an OFFSET from the header: that is what the
    4-byte pointers are on iOS, p32rebase.py), which of the user's files it comes from, where in it, how long.
    The same list as android.py finish(), whose notes hold for the details."""
    if not (DATA / "SKELETON").exists():
        return
    syms = symbols()
    where, checksum = {}, None
    for l in (ROOT / "port/data/index.txt").read_text().splitlines():
        p = l.split()
        if len(p) == 3 and not l.startswith("#"):
            where[p[0]] = (int(p[1]), int(p[2], 16))
        elif len(p) == 2 and p[0] == "checksum":
            checksum = int(p[1], 16)
    records = []
    for name in sorted(where):
        which, so = where[name]
        text = (ROOT / "port/data" / (name + ".s")).read_text().splitlines()
        first, before = None, 0
        for l in text:
            if re.match(r"^[A-Za-z_.$][\w.$]*:$", l.strip()):
                first = l.strip()[:-1]
                break
            p = l.split()
            if p and p[0] in (".space", ".zero"):
                before += int(p[1])
            elif p and p[0] == ".long":
                before += 4
        if first is None or first not in syms:
            raise SystemExit(f"{name}: its first label ({first}) is not in the program")
        start, off = syms[first] - before, 0
        for l in text:
            p = l.split()
            if not p or p[0].startswith("#"):
                continue
            if p[0] == ".space":
                records.append((start + off, which, so + off, int(p[1])))
                off += int(p[1])
            elif p[0] == ".zero":
                off += int(p[1])
            elif p[0] == ".long":
                off += 4
    dat = bytearray(b"BT3D" + struct.pack("<IQ", len(records), checksum))
    for r in records:
        dat += struct.pack("<IIII", *[x & 0xFFFFFFFF for x in r])
    pathlib.Path(str(EXE) + ".dat").write_bytes(dat)
    print(f"data list: {len(records)} records -> {EXE.name}.dat")


def main():
    objs = [str(o) for o in sorted(OBJ.glob("*.o")) + sorted(DATA.glob("*.o"))]
    if not objs:
        raise SystemExit(f"no objects in {OBJ}: run `BT3_PLATFORM=ios python3 port/tools/android.py compile` first")
    r = sh(link_cmd(objs, []))
    need = undefined(r.stderr)
    if r.returncode and not need:
        print(r.stderr[-3000:])
        return 1
    GEN.mkdir(parents=True, exist_ok=True)
    lines = ["/* Generated by port/tools/ios_link.py: one stub per symbol the iOS build does not provide yet. */",
             "extern long write(int, const void *, unsigned long);", "extern unsigned long strlen(const char *);", "extern void abort(void);",
             'static void missing(const char *n) { write(2, "bt3: not implemented yet: ", 26); write(2, n, strlen(n)); write(2, "\\n", 1); abort(); }']
    lines += [f'void {s}(void) {{ missing("{s}"); }}' for s in need]
    (GEN / "stubs.c").write_text("\n".join(lines) + "\n")
    (OUT / "stubs.txt").write_text("\n".join(need) + "\n")
    extra = []
    if need:
        s = sh(["xcrun", "--sdk", "iphoneos", "clang"] + target() + ["-c", "-O2", "-fPIE", GEN / "stubs.c", "-o", GEN / "stubs.o"])
        if s.returncode:
            print(s.stderr[:800])
            return 1
        extra = [str(GEN / "stubs.o")]
    r = sh(link_cmd(objs, extra))
    print(f"{len(need)} stubs (port/build/ios/stubs.txt); link {'OK -> ' + str(EXE.relative_to(ROOT)) if r.returncode == 0 else 'FAILED'}")
    if r.returncode:
        print("\n".join(l[-220:] for l in r.stderr.splitlines()[:40]))
        return 1
    if r.stderr.strip():
        print("linker warnings (first lines):", *[l[-200:] for l in r.stderr.splitlines()[:8]], sep="\n  ")
    write_data_list()
    return 0


if __name__ == "__main__":
    sys.exit(main())
