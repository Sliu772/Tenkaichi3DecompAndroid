#!/usr/bin/env python3
"""Packages the iOS build (BT3_CC=ios64) as an .app and an unsigned .ipa (a sideloading tool — Sideloadly,
SideStore, AltStore — signs it with the user's certificate and installs it on the phone).

    BT3_CC=ios64 port/tools/undefined.py && BT3_CC=ios64 port/tools/link.py && port/tools/ios_app.py

The bundle carries the two files the program reads next to itself, both written by link.py: the .dat list of its
stripped data (make_dat.py) and the .mem state map (make_state.py). The Documents folder the app shows in the Files
app (UIFileSharingEnabled) is its working folder: the user's .iso, gamedata/ and bt3_log.txt live there
(port/src/plat_ios.c).
"""
import os, pathlib, plistlib, shutil, sys, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
B = ROOT / "port/build"
EXE = B / "bt3_ios"
OUT = B / "ios"
APP = OUT / "Payload" / "DragonRage.app"
IPA = OUT / "DragonRage.ipa"

def main():
    if not EXE.exists():
        sys.exit(f"ios_app.py: {EXE} is not there (BT3_CC=ios64 port/tools/undefined.py && BT3_CC=ios64 port/tools/link.py)")
    shutil.rmtree(OUT, ignore_errors=True)
    APP.mkdir(parents=True)
    shutil.copy2(EXE, APP / EXE.name)
    os.chmod(APP / EXE.name, 0o755)
    for extra, what in ((B / (EXE.name + ".dat"), "the list of its data (the game's data cannot be read without it)"),
                        (B / (EXE.name + ".mem"), "the state map (saving and loading states)")):
        if extra.exists():
            shutil.copy2(extra, APP / extra.name)
        else:
            print(f"ios_app.py: warning: {extra.name} is not there: no {what}")
    plist = {
        "CFBundleExecutable": EXE.name,
        "CFBundleIdentifier": "com.ggg4.dragonrage",
        "CFBundleName": "Dragon Rage",
        "CFBundleDisplayName": "Dragon Rage",
        "CFBundleVersion": "1",
        "CFBundleShortVersionString": "0.6",
        "CFBundlePackageType": "APPL",
        "CFBundleInfoDictionaryVersion": "6.0",
        "MinimumOSVersion": os.environ.get("BT3_IOS_MIN", "15.0"),
        "UIDeviceFamily": [1, 2],
        "UIRequiredDeviceCapabilities": ["arm64"],
        "UISupportedInterfaceOrientations": ["UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"],
        "UIRequiresFullScreen": True,
        "UIStatusBarHidden": True,
        "UILaunchScreen": {},
        "LSRequiresIPhoneOS": True,
        "UIFileSharingEnabled": True,
        "LSSupportsOpeningDocumentsInPlace": True,
    }
    (APP / "Info.plist").write_bytes(plistlib.dumps(plist))
    with zipfile.ZipFile(IPA, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(APP.rglob("*")):
            z.write(f, f.relative_to(OUT))
    total = sum(f.stat().st_size for f in APP.rglob("*") if f.is_file())
    print(f"ios_app.py: {IPA.relative_to(ROOT)} ({total / (1 << 20):.1f} MB, unsigned: the sideloading tool signs it)")
    for f in sorted(APP.rglob("*")):
        if f.is_file():
            print(f"  {f.relative_to(APP)} {f.stat().st_size}")

main()
