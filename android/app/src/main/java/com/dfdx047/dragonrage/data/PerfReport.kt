package com.dfdx047.dragonrage.data

import java.io.File

/**
 * The game's last run as numbers, from its own log (bt3_log.txt): every 300 pictures the engine writes how fast the
 * game ran ("bt3: speed 97% (58.5 game blanks/s, 29.2 pictures/s over 10.3 s, fight)"), at no cost worth measuring.
 * 100 % is the console's speed (60 game blanks a second, 30 pictures in a fight).
 */
data class PerfReport(
    val gpu: String,
    val renderer: String,
    val resolution: String,
    val fightSamples: Int,
    val fightAvg: Int,
    val fightMin: Int,
    val fightFps: Double,
    val menuAvg: Int,
    val lines: List<String>,
) {
    companion object {
        private val speed = Regex("""bt3: speed (\d+)% \(([\d.]+) game blanks/s, ([\d.]+) pictures/s over ([\d.]+) s(?:, ([^);]*))?""")

        fun read(paths: GamePaths): PerfReport? {
            val log = File(paths.root, "bt3_log.txt")
            if (!log.exists()) return null
            var gpu = ""
            var renderer = ""
            var resolution = ""
            val fight = mutableListOf<Pair<Int, Double>>()
            val menus = mutableListOf<Int>()
            val last = ArrayDeque<String>()
            log.forEachLine { l ->
                when {
                    l.contains("bt3: GPU: ") -> gpu = l.substringAfter("bt3: GPU: ").substringBefore(", driver").trim()
                    l.contains("bt3: GL renderer: ") -> gpu = l.substringAfter("bt3: GL renderer: ").substringBefore(" (").trim()
                    l.contains("bt3: GPU renderer: ") -> renderer = l.substringAfter("bt3: GPU renderer: ").trim()
                    l.contains("bt3: internal resolution ") -> resolution = l.substringAfter("bt3: internal resolution ").trim()
                }
                val m = speed.find(l) ?: return@forEachLine
                val pct = m.groupValues[1].toInt()
                val secs = m.groupValues[4].toDoubleOrNull() ?: 0.0
                val note = m.groupValues[5]
                if (secs > 30.0 || note.contains("menu was open")) return@forEachLine // (in the background, or paused)
                if (note == "fight") fight += pct to (m.groupValues[3].toDoubleOrNull() ?: 0.0) else menus += pct
                last.addLast(l.substringAfter("bt3: ").take(140))
                if (last.size > 12) last.removeFirst()
            }
            return PerfReport(
                gpu = gpu,
                renderer = renderer,
                resolution = resolution,
                fightSamples = fight.size,
                fightAvg = if (fight.isEmpty()) 0 else fight.sumOf { it.first } / fight.size,
                fightMin = fight.minOfOrNull { it.first } ?: 0,
                fightFps = if (fight.isEmpty()) 0.0 else fight.sumOf { it.second } / fight.size,
                menuAvg = if (menus.isEmpty()) 0 else menus.sum() / menus.size,
                lines = last.toList(),
            )
        }
    }
}
