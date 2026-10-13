# iOS port: state and what is left

Branch `ios-prototype`, CI `.github/workflows/ios.yml` (jobs: abi-check, build-sdl3, engine-compile, link-ipa).

## Done
- Engine compiles for arm64 iOS (soft-float ABI, 4-byte pointers as offsets from `__mh_execute_header`, `p32rebase.py`).
- Step 2 (this file's commit): `ios_link.py` links a Mach-O (`bt3`) + `bt3.dat`, `ios_package.sh` writes an unsigned `bt3.ipa`.
  Whatever the engine still needs is stubbed and listed in `port/build/ios/stubs.txt` (job summary).
- `plat_ios.c`: log to `Documents/bt3_log.txt`, crash handler, window probe, `main()` -> `SDL_RunApp`.
- ld64 has no `--wrap`/`--defsym`: `main` and `Progress_Main` are renamed at preprocessing (`IOS_RENAMES` in android.py).

## Done in the second step (memory model, renderer plumbing)
- `plat_mem.c`: heap (32 MB), low arena (192 MB) and the game's stack (16 MB) are ONE block found by `Ios_MapWindow`
  (plat_ios.c) inside `[header, header + 4 GB)`, above the image. `Port_Malloc` / `Port_LowAlloc` hand out real addresses (the
  call boundary turns them into offsets), `Port_GameBigAlloc/Free` and the heap bookkeeping use offsets. A guard page sits
  under the stack.
- The game runs on the MAIN thread, on its own stack: `Port_CallOnStack` (arm64 asm) switches `sp`. No game thread.
- `gPortHwRegs` / `gPortGsRegs` exist on iOS (arrays in the engine, as on Android); no scratchpad is mapped.
- `Port_LoadGameData`: `_NSGetExecutablePath` instead of `/proc/self/exe`; `bt3.dat` offsets are added to the header.
- `port_host.h`: `Port_A2P` / `Port_P2A` convert the game's 4-byte addresses (offsets on iOS) for host code that is handed
  them as integers (DMA tags, name font, sound banks, SIF DMA). Any new host code that reads an address out of the game's
  memory must use them.
- `D_3BE71C` is the number 0x3BE71C (`p32rebase.FIXED`, `ios_pp.FIXED_ADDRESSES`), not a symbol.
- No OpenGL on iOS: `gs_nogl.c` (empty stand-ins), no `imgui_impl_opengl3.cpp`, `ui.cpp` stubs. `headless.c`'s ELF-only dump is off.
- Renderer: SDL_GPU's Vulkan driver through MoltenVK 1.4.2 (`MoltenVK.framework` in `Frameworks/`, path given to SDL with
  `SDL_VULKAN_LIBRARY` in `plat_ios.c`). The Metal driver would need MSL shaders instead of SPIR-V (not done).

## Known gaps for the next step
1. The game's data is installed ON THE DEVICE (`port/src/ios_install.h`, called from `plat_ios.c` before the game starts): the
   user copies their USA `.iso` (SLUS-21678, 2048-byte sectors) into Files app > On My iPhone > BT3. If
   `Documents/gamedata/disc/SLUS_216.78` and `disc/BIN/DBZP.BIN` are missing, the app reads the ISO 9660 tree and copies
   SLUS_216.78, BIN/* and DATA/* (the AFS archives whole, as on Android) to `gamedata/disc/`, with a progress screen
   (SDL_RenderDebugText). `gamedata/.installing` marks a copy that was cut short (it is redone); a finished one leaves
   `gamedata/.installed`. Errors (no ISO, not an ISO, other release, no space) stay on the screen and in `bt3_log.txt`.
   Tested on Linux with a synthetic ISO (files past 4 GB included); the SDL screen is unverified on a device.
   The game's programs are read in `__wrap_main` (after the install), not in `plat_mem.c`'s constructor, which runs before
   `main()` and could only exit with "the game's program was not found". Not done: .bin/.cue images (convert to .iso), checking the hash at install time (the engine does it at start).
2. `stubs.txt` still lists the `func_00xxxxxx` entries of the data tables and `dvCi*` (the same stubs as Android) and the
   `gl*` calls of anything that still reaches OpenGL; check it after each run.
3. State save/restore (`gs/state.c`) and the net rollback still assume PS2 addresses for their regions: not looked at yet.
4. `gPortDataStripped` is not patched to 1 in the Mach-O yet (a missing `bt3.dat` is not fatal).
5. Touch controls (Android has `TouchBridge`): a game controller works through SDL meanwhile. Performance target 60 fps.
6. Unverified on a device: the stack switch on the main thread (UIKit's run loop runs on it while SDL pumps events) and
   MoltenVK's device creation (look for `bt3 iOS: Vulkan library ... found` and the `bt3: Vulkan device` lines in the log).
