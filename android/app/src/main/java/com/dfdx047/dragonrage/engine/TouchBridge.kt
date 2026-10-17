package com.dfdx047.dragonrage.engine

import android.util.Log

/** The on-screen controller's line to the engine (functions in libmain.so, src/main/cpp/loader.c). */
object TouchBridge {
    /** Player 1's buttons (PS2 bits, 1 = pressed) and sticks (0..255, 128 = centre). */
    @JvmStatic external fun nativeSetPad(buttons: Int, lx: Int, ly: Int, rx: Int, ry: Int)

    /** The engine's own window (settings, online) is open. */
    @JvmStatic external fun nativeMenuOpen(): Boolean

    /** Player-side team size in battle; 0 = no active battle, -1 = active state could not be read reliably. */
    @JvmStatic external fun nativePlayerTeamSize(): Int

    private var loggedTeamQueryFailure = false

    private var nativeMissing = false

    fun playerTeamSize(): Int = if (nativeMissing) -1 else try {
        nativePlayerTeamSize().coerceIn(-1, 5)
    } catch (t: Throwable) {
        // (the launcher's own process has no engine: ask once, not twice a second with an error each time)
        if (t is UnsatisfiedLinkError) nativeMissing = true
        // The previous implementation swallowed UnsatisfiedLinkError and left the team buttons hidden forever.
        if (!loggedTeamQueryFailure) {
            loggedTeamQueryFailure = true
            Log.e("bt3", "touch roster: native team-size query failed", t)
        }
        -1
    }

    fun setPad(buttons: Int, lx: Int, ly: Int, rx: Int, ry: Int) {
        runCatching { nativeSetPad(buttons, lx, ly, rx, ry) }
    }

    fun menuOpen(): Boolean = runCatching { nativeMenuOpen() }.getOrDefault(false)
}
