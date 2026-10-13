# iOS port: state and what is left

Branch `ios-prototype`, CI `.github/workflows/ios.yml` (jobs: abi-check, build-sdl3, engine-compile, link-ipa).

## Done
- Engine compiles for arm64 iOS (soft-float ABI, 4-byte pointers as offsets from `__mh_execute_header`, `p32rebase.py`).
- Step 2 (this file's commit): `ios_link.py` links a Mach-O (`bt3`) + `bt3.dat`, `ios_package.sh` writes an unsigned `bt3.ipa`.
  Whatever the engine still needs is stubbed and listed in `port/build/ios/stubs.txt` (job summary).
- `plat_ios.c`: log to `Documents/bt3_log.txt`, crash handler, window probe, `main()` -> `SDL_RunApp`.
- ld64 has no `--wrap`/`--defsym`: `main` and `Progress_Main` are renamed at preprocessing (`IOS_RENAMES` in android.py).

## Known gaps for the next step (memory model, plat_mem.c)
1. `plat_mem.c` still maps at fixed PS2/low addresses (stack 0x02000000, regs 0x10000000/0x12000000, scratchpad 0x70000000,
   low arena): all of that is inside `__PAGEZERO` on iOS. Everything must be placed in `[header, header+4GB)` and converted
   with `ptr - header` (the log of the first run says which sizes fit and at which offsets).
2. `D_3BE71C` (src/sys/heap.c: `size = 0x1EFB000 - (u32)D_3BE71C`) must come out as the number 0x3BE71C. With offsets it
   needs a symbol at `header + 0x3BE71C` (not an absolute symbol: p32rebase turns symbols into `sym - header`).
3. The game thread: its stack must be in the window too. SDL on iOS wants UIKit calls on the main thread, so the plan is to
   keep the game on the main thread and switch the stack pointer there (`Port_CallOnStack` in arm64 asm, as the Windows build does).
4. `Port_LoadGameData` finds the executable with `/proc/self/exe` (use `_NSGetExecutablePath`) and the data list holds header
   offsets here, not addresses.
5. `gPortDataStripped` is not patched to 1 in the Mach-O yet (a missing `bt3.dat` is not fatal).
6. Renderer: Vulkan through MoltenVK (SDL3's loader) first, a Metal backend afterwards; then performance (target 60 fps).
