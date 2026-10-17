package com.dfdx047.dragonrage.data

import org.json.JSONArray

/** A driver package published on GitHub: one asset (.zip or .adpkg) of a release. */
data class DriverAsset(val name: String, val url: String, val size: Long)

data class DriverRelease(val title: String, val tag: String, val date: String, val assets: List<DriverAsset>)

/** Where drivers come from: the GitHub repositories of the people who build them. */
enum class DriverSource(val label: String, val repo: String) {
    MR_PURPLE("Mr. Purple", "MrPurple666/purple-turnip"),
    STEVEN("StevenMXZ", "StevenMXZ/Adreno-Tools-Drivers"),
    KIMCHI("K11MCH1", "K11MCH1/AdrenoToolsDrivers"),
    ;

    val page: String get() = "https://github.com/$repo/releases"
}

object DriverSources {
    /** The newest releases of a source that have a driver package to download (GitHub's API, no login). */
    suspend fun releases(source: DriverSource, count: Int = 15): List<DriverRelease> {
        val a = JSONArray(Net.getText("https://api.github.com/repos/${source.repo}/releases?per_page=$count"))
        val out = mutableListOf<DriverRelease>()
        for (i in 0 until a.length()) {
            val r = a.optJSONObject(i) ?: continue
            if (r.optBoolean("draft")) continue
            val assets = mutableListOf<DriverAsset>()
            r.optJSONArray("assets")?.let { list ->
                for (j in 0 until list.length()) {
                    val x = list.optJSONObject(j) ?: continue
                    val name = x.optString("name")
                    if (!name.endsWith(".zip", true) && !name.endsWith(".adpkg", true)) continue
                    assets += DriverAsset(name, x.optString("browser_download_url"), x.optLong("size"))
                }
            }
            if (assets.isEmpty()) continue
            out += DriverRelease(
                title = r.optString("name").ifBlank { r.optString("tag_name") },
                tag = r.optString("tag_name"),
                date = r.optString("published_at").take(10),
                assets = assets,
            )
        }
        return out
    }
}
