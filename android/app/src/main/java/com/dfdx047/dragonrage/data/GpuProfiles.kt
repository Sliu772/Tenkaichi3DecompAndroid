package com.dfdx047.dragonrage.data

/**
 * What the game can ask of a GPU. The launcher reads the GPU's name (Renderer.gpuName: GL_RENDERER, e.g. "Mali-G68 MC4",
 * "Adreno (TM) 619", "Samsung Xclipse 530") and finds its tier here; on the first start with a known GPU it offers that
 * tier's settings, made to keep fights at a steady 30 pictures a second (the game's own rate). Nothing is changed
 * without the player's yes, and everything stays changeable in Settings.
 *
 * The tiers come from the GPUs' known performance and from the logs players sent: Mali-G52 MC2 at about 65 % speed at
 * 2x, Mali-G68 MC4 (Exynos 1380) 25-30 % at 2x, Xclipse 530 40-60 % at 2x, Mali-G720 MC7, Adreno 825 and 830 at
 * 100 % at 2x and more.
 */
object GpuProfiles {
    /** 0 very weak, 1 weak, 2 mid-range, 3 strong, 4 flagship. */
    data class Profile(
        val gpu: String,
        val tier: Int,
        val scale4: Int,        // internal resolution in quarters (4 = 1x)
        val interp: Int,        // smooth motion: 0 off, 1 = 60
        val fragDepth: Int,     // precise depth
        val frameSkip: Int,     // skip pictures instead of slowing down
        val fxOffAdd: Int,      // PS2 effects switched off (bits: 16 distance blur, 32 movement blur)
        val filter: Int,        // screen filter: 0 bilinear
    )

    /** [family]: a regular expression over the lower-case name; [from]..[to]: its three-digit model number (0..0: any). */
    private data class Rule(val family: String, val from: Int, val to: Int, val tier: Int)

    private fun mali(model: String, tier: Int) = Rule("mali-$model(?!\\d)", 0, 0, tier)

    // Checked in order (the first that matches).
    private val rules = listOf(
        // Qualcomm Adreno
        Rule("adreno", 300, 499, 0), Rule("adreno", 500, 512, 0), Rule("adreno", 530, 530, 1), Rule("adreno", 540, 540, 2),
        Rule("adreno", 600, 612, 0), Rule("adreno", 613, 620, 1), Rule("adreno", 630, 630, 2), Rule("adreno", 640, 644, 2),
        Rule("adreno", 650, 669, 3), Rule("adreno", 680, 699, 3), Rule("adreno", 702, 712, 1), Rule("adreno", 720, 729, 2),
        Rule("adreno", 730, 739, 3), Rule("adreno", 740, 799, 4), Rule("adreno", 800, 899, 4),
        // Arm Mali (Midgard, Bifrost, Valhall, 5th gen) and Immortalis
        Rule("mali-t\\d", 0, 0, 0), mali("g31", 0), mali("g51", 0), mali("g52", 0), mali("g57", 0), mali("g71", 1), mali("g72", 1),
        mali("g68", 1), mali("g76", 2), mali("g77", 2), mali("g78", 2), mali("g610", 2), mali("g615", 2), mali("g710", 3),
        mali("g715", 3), mali("g720", 3), mali("g725", 3), mali("g925", 4), Rule("immortalis", 0, 0, 4),
        // Samsung Xclipse (AMD RDNA)
        Rule("xclipse", 500, 599, 1), Rule("xclipse", 900, 929, 3), Rule("xclipse", 930, 999, 4),
        // Imagination PowerVR (GE8320, GM9446, BXM-8-256, DXT-48-1536 ...)
        Rule("\\bg[em]?\\d{4}", 0, 0, 0), Rule("bxe", 0, 0, 0), Rule("bxm", 0, 0, 1), Rule("dxt", 0, 0, 2),
        // Huawei
        Rule("maleoon", 0, 0, 2),
    )

    /** A Mali model with few cores (MC1, MC2) is one tier lower than its family; one with many is one higher. */
    private fun maliCores(name: String): Int = Regex("""MC(\d+)|MP(\d+)""", RegexOption.IGNORE_CASE).find(name)
        ?.groupValues?.drop(1)?.firstOrNull { it.isNotEmpty() }?.toIntOrNull() ?: 0

    /** The tier of a GPU by its name; -1 when it is not known. */
    fun tierOf(gpu: String): Int {
        val n = gpu.lowercase().replace("(tm)", "").replace(Regex("\\s+"), " ").trim()
        val number = Regex("""(\d{3})""").find(n)?.groupValues?.get(1)?.toIntOrNull() ?: 0
        for (r in rules) {
            if (!Regex(r.family).containsMatchIn(n)) continue
            if (r.from == 0 && r.to == 0 || number in r.from..r.to) {
                var t = r.tier
                if (n.startsWith("mali")) {
                    val cores = maliCores(gpu)
                    if (cores in 1..2 && t > 0) t--
                    if (cores >= 10 && t < 4) t++
                }
                return t
            }
        }
        return -1
    }

    fun profile(gpu: String): Profile? {
        val t = tierOf(gpu)
        return when (t) {
            0 -> Profile(gpu, 0, scale4 = 4, interp = 0, fragDepth = 0, frameSkip = 1, fxOffAdd = 16 or 32, filter = 0)
            1 -> Profile(gpu, 1, scale4 = 4, interp = 0, fragDepth = 0, frameSkip = 1, fxOffAdd = 32, filter = 0)
            2 -> Profile(gpu, 2, scale4 = 6, interp = 0, fragDepth = 0, frameSkip = 1, fxOffAdd = 0, filter = 0)
            3 -> Profile(gpu, 3, scale4 = 8, interp = 0, fragDepth = 1, frameSkip = 1, fxOffAdd = 0, filter = 0)
            4 -> Profile(gpu, 4, scale4 = 8, interp = 1, fragDepth = 1, frameSkip = 1, fxOffAdd = 0, filter = 0)
            else -> null
        }
    }

    /** Writes the profile's settings (the player said yes); the tier is kept so that it is not offered again. */
    fun apply(p: Profile, s: EngineSettings) {
        s.set(EngineSettings.SCALE4, p.scale4)
        s.set(EngineSettings.SCALE, (p.scale4 + 2) / 4)
        s.set(EngineSettings.INTERP, p.interp)
        s.set(EngineSettings.FRAG_DEPTH, p.fragDepth)
        s.set(EngineSettings.FRAME_SKIP, p.frameSkip)
        s.set(EngineSettings.FX_OFF, s.get(EngineSettings.FX_OFF, 0) or p.fxOffAdd)
        s.set(EngineSettings.FILTER, p.filter)
        s.set(EngineSettings.GPU_TIER, p.tier)
    }
}
