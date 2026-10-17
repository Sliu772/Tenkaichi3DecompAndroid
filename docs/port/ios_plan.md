# iOS port: plan

Base: this repository (0.6, the first commit of this branch is a copy of `Tenkaichi3DecompAndroid-main`, untouched).
The macOS/arm64 port is the template (`docs/port/macos_plan.md`): the same pipeline (ptr32, the `macwin` LLVM
pass, software float through `llc` without a floating-point unit, `macho.py` tables, `make_state`/`make_dat`) and
the same SDL3 + SDL_GPU renderer are reused, with MSL shaders and SDL_GPU's **Metal** back end (the prototype's
MoltenVK is not needed: 0.6 already translates the shaders to MSL).

Variant name: `BT3_CC=ios64` -> `port/build/bt3_ios`, and `port/tools/ios_app.py` turns it into the `.app`.
Build machine: a Mac with Xcode and Homebrew's LLVM 22 (the toolchain of the macOS build); CI runs it in
`.github/workflows/ios.yml` on a macOS runner and uploads an unsigned `.ipa` as an artifact.

## What iOS changes against macOS

- **Target**: `--target=arm64-apple-ios<min>` with the iPhoneOS SDK (`xcrun --sdk iphoneos`), `llc`
  `-mtriple=arm64-apple-ios<min>`, and SDL3 built from source for iOS (static, in `port/build/ios/`).
- **Memory model**: the same 4 GB window and 4-byte game pointers (an address is an offset into the window), but
  the window starts at the image header: `W = __mh_execute_header` (`-bt3-offset=0` in the pass, i.e. the
  prototype's "offsets from the header"). Reasons, both iOS rules:
  - the first 4 GB of every process stay unmapped (`__PAGEZERO`), so nothing can be mapped at the PS2's own
    low addresses nor below the image;
  - the main binary is slid on every launch and cannot be re-executed with ASLR off (`window_aligned`, the
    macOS arrangement only), so the window cannot be 4 GB-aligned and its start cannot be chosen.
  Consequently the low zones (PS2 heap 32 MB, the low arena 192 MB, the game's stack 16 MB, the register
  stand-ins) are ONE block found above the image inside `[header, header + 4 GB)`, as the prototype found; the
  port's allocators hand out game addresses inside that block (the call boundary turns host pointers into
  offsets as on macOS, `Port_Ptr`/`Port_Addr`). `D_3BE71C` stays the number 0x3BE71C.
  Constant data (`port/data/*.s`, `macho.py`) keeps pointers as `_g - __mh_execute_header` (slide-free; the
  runtime form is `W + offset` with W loaded from `Port_Window`).
- **Link**: ld64 through clang (as `link_mac`) without the `-segaddr` moves (the image keeps the platform's own
  layout) and with the iOS SDK and frameworks (SDL3, UIKit, Metal, QuartzCore, CoreAudio, AudioToolbox,
  AVFoundation for the movies when they come). `-export_dynamic` is dropped if the linker objects.
- **No OpenGL, no JIT**: SDL_GPU/Metal only (the `gs_nogl.c`-style stand-ins for anything that still calls
  OpenGL), and everything is AOT (the pipeline already is).
- **Shell**: SDL3 owns the window and the app lifecycle (`SDL_RunApp`, UIKit behind it); the game's main runs on
  the main thread with its own stack as on macOS (`Port_CallOnStack`). Touch input maps to the pad (SDL3
  finger events -> `gs_input.c`); a Bluetooth/USB pad works through SDL3 unchanged. Sound: SDL3 audio
  (CoreAudio) opened as on macOS.
- **Data on the device**: the user copies their own USA `.iso` (SLUS-21678) into the app's Documents through
  the Files app (`UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace`); the app extracts
  `SLUS_216.78`, `BIN/`, `DATA/` to `Documents/gamedata/` with a progress screen on first run (SDK-free:
  SDL's debug text/ImGui), and reads loose files from there afterwards, as the prototype did. `.bin/.cue`
  images must be converted by the user.
- **Package**: `bt3.app` (Info.plist, icon, SDL3 static, ad-hoc signature) zipped as an unsigned `.ipa`; the
  user signs it at install (Sideloadly/AltStore). No entitlements are needed or possible.

## Build pipeline (`BT3_CC=ios64`)

`port/tools/toolchain.py` gains the variant: `IOS`, target triple, SDK (xcrun), `APPLE` = MAC | IOS for the
shared branch (soft float via `llc -mattr`, no FPU). `undefined.py` treats it as the macOS build with the iOS
triple and `-bt3-offset=0`; `macwin.py`/`macwin.cpp` gain the offset option (the pass is otherwise unchanged);
`link.py` links through `link_mac` minus `-segaddr` with the iOS SDK; `macho.py` writes `_g -
__mh_execute_header` (offset 0); `make_state.py`/`make_dat.py` read their game addresses with the same offset.
`port/tools/ios_app.py` assembles the bundle and the `.ipa`.

## Ordered work

1. Toolchain: `ios64` variant + pass offset + link/asm branches (this file can go stale: update it).
2. Platform: `plat_mem.c` iOS branch (the block above the image, `mach_vm_region` walk, 16 KB pages, SIGBUS +
   SIGSEGV in `plat_crash.c`), `plat_file.c`/`plat_sys.c` paths under `Documents/`, log file, touch input,
   SDL3 audio; `plat_stub.c` movies off until AVFoundation.
3. First CI build: compile, link, stub list (`port/build/ios/stubs.txt` in the run summary), `.app`, `.ipa`.
4. On the device: video, input, sound, performance; the ISO import screen; the fight hash against the same
   build's own headless run.
5. Then the game-side leftovers the prototype listed: state save/restore addresses, `gPortDataStripped`,
   `Port_A2P`/`Port_P2A` at every host boundary that reads a game address from memory.

## Known risks (record and re-check on device)

- Game code that compares raw addresses against hardcoded ranges (the prototype lived with the offset-from-
  header model; anything found goes behind `#ifdef PORT` in `src/` or is converted in the port, never patched
  into the matching build).
- `W + offset` places the low zones just above the image: slightly different values than Linux, as between any
  two builds; the fight-state hash (`BT3_HASH`) must match the same build's headless run, not Linux's memory
  checksum (that one holds addresses and differs between platforms anyway, se `macos_plan.md`).
- App Store is not a target; nothing depends on it.
