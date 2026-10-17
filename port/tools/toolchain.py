"""Which build this is and which tools it uses; shared by undefined.py, link.py and strip_data.py.

    BT3_CC=gcc       (default) 32-bit, gcc                      -> port/build/bt3
    BT3_CC=clang     32-bit, the game code through clang        -> port/build/bt3_clang
    BT3_CC=clang64   64-bit Linux                               -> port/build/bt3_64
    BT3_CC=win64     64-bit Windows                             -> port/build/bt3.exe
                     On Linux: cross-compiled (mingw-w64: x86_64-w64-mingw32-gcc, clang with the mingw target; SDL3's
                     mingw development package unpacked under port/build/win or named by BT3_SDL3).
                     On Windows: in the MSYS2 CLANG64 or UCRT64/MINGW64 shell, with that shell's own tools.
    BT3_CC=mac64     64-bit macOS on Apple Silicon (arm64)      -> port/build/bt3_mac
                     Homebrew's LLVM (`brew install llvm`; BT3_LLVM names another bin/ folder): Apple's clang drops
                     the 4-byte pointers on arm64. The game's addresses are offsets in a 4 GB window instead of
                     low addresses (macOS keeps the low 4 GB unmapped): see docs/port/macos_plan.md.
    BT3_CC=ios64     arm64 iOS, device (BT3_IOS_MIN, 15.0)      -> port/build/bt3_ios
                     The same pipeline as mac64, with the iPhoneOS SDK and the window starting at the program's
                     own header (the system slides the program on every launch and keeps the low 4 GB unmapped):
                     BT3_SDL3 names the folder with SDL3 built for iOS. See docs/port/ios_plan.md."""
import os, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
VARIANT = os.environ.get("BT3_CC", "gcc")
WIN = VARIANT == "win64"
MAC = VARIANT == "mac64"
IOS = VARIANT == "ios64"
APPLE = MAC or IOS
BITS64 = VARIANT in ("clang64", "win64", "mac64", "ios64")
ON_WINDOWS = os.name == "nt" or sys.platform in ("msys", "cygwin")
PREFIX = "x86_64-w64-mingw32-" if WIN and not ON_WINDOWS else ""
MAC_TRIPLE = "arm64-apple-macos11"
IOS_TRIPLE = "arm64-apple-ios" + os.environ.get("BT3_IOS_MIN", "15.0")
TRIPLE = MAC_TRIPLE if MAC else IOS_TRIPLE if IOS else None
# Where the program starts in the game's 4 GB window (port/tools/macwin/macwin.cpp): 0x20000000, as on Linux and
# Windows (at 1 TB on macOS); 0 on iOS, where the window starts at the program's own header (the low 4 GB stay
# unmapped and the program is slid on every launch). macwin.py, macho.py, make_state.py and make_dat.py use it.
PROGRAM_OFFSET = 0x0 if IOS else 0x20000000
# iOS: how far above its own header the program places the two zones the PS2 keeps right after its program (the
# game heap and the game's stack: port/src/plat_mem.c; src/sys/heap.c's heap size moves with it). Large enough for
# any program, small enough to leave the registers (0x10000000), the port's region (0x13000000) and the scratchpad
# (0x70000000) at their PS2 addresses, where the game expects them.
LOW_SHIFT = 0x08000000 if IOS else 0x0
# What every Apple compile gets on its command line: which window the game's addresses are offsets in (macwin's
# -bt3-offset, macho.py, make_state.py) and where the low zones are (port_addr.h, plat_mem.c, heap.c).
APPLE_DEFINES = [f"-DBT3_WINDOW_OFFSET={PROGRAM_OFFSET:#x}", f"-DBT3_LOW_SHIFT={LOW_SHIFT:#x}"] if APPLE else []
LLVM = pathlib.Path(os.environ.get("BT3_LLVM", "/opt/homebrew/opt/llvm/bin")) if APPLE else None

def llvm(tool):
    """An LLVM tool by name: Homebrew's for the Apple builds, the one on PATH otherwise."""
    return str(LLVM / tool) if APPLE else tool

def mac_sdk():
    return subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()

def ios_sdk():
    return subprocess.run(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()

def apple_sdk():
    """The SDK of this Apple build: the device's for iOS, the Mac's otherwise."""
    return ios_sdk() if IOS else mac_sdk()

# -DBT3_IOS marks the iOS build for the port's own sources (the platform layer switches on it); <TargetConditionals.h>
# on the device says the same.
CLANG_TARGET = (["--target=x86_64-w64-mingw32"] if WIN else
                [f"--target={MAC_TRIPLE}", "-isysroot", mac_sdk()] if MAC else
                [f"--target={IOS_TRIPLE}", "-isysroot", ios_sdk(), "-DBT3_IOS=1"] if IOS else [])
OBJ = ROOT / ("port/build/obj" if VARIANT == "gcc" else "port/build/obj_" + VARIANT)
DATA = ROOT / ("port/build/obj_data" if not BITS64 else "port/build/obj_data_win64" if WIN else
               "port/build/obj_data_mac64" if MAC else "port/build/obj_data_ios64" if IOS else "port/build/obj_data64")
EXE = ROOT / "port/build" / {"gcc": "bt3", "clang64": "bt3_64", "win64": "bt3.exe", "mac64": "bt3_mac", "ios64": "bt3_ios"}.get(VARIANT, "bt3_" + VARIANT)

def sdl3():
    """The folder with SDL3's include/ and lib/ for the Windows build, or None when the compiler finds it itself."""
    if not WIN:
        return None
    if os.environ.get("BT3_SDL3"):
        return pathlib.Path(os.environ["BT3_SDL3"])
    found = sorted((ROOT / "port/build/win").glob("SDL3-*/x86_64-w64-mingw32"))
    return found[-1] if found else None

def apple_sdl3():
    """The folder with SDL3's include/ and lib/ for the Apple builds: BT3_SDL3 (required for iOS: SDL3 built for it,
    include/ and lib/libSDL3.a), or Homebrew's SDL3."""
    if os.environ.get("BT3_SDL3"):
        return pathlib.Path(os.environ["BT3_SDL3"])
    return pathlib.Path("/opt/homebrew/opt/sdl3")
