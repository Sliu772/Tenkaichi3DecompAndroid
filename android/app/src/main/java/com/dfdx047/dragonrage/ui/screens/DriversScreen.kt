package com.dfdx047.dragonrage.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.DeveloperBoard
import androidx.compose.material.icons.rounded.Download
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Upload
import androidx.compose.material.icons.rounded.Warning
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.dfdx047.dragonrage.AppViewModel
import com.dfdx047.dragonrage.R
import com.dfdx047.dragonrage.data.DriverSource
import com.dfdx047.dragonrage.data.formatBytes
import com.dfdx047.dragonrage.ui.components.ScreenTitle
import com.dfdx047.dragonrage.ui.components.SectionCard
import com.dfdx047.dragonrage.ui.theme.Dbz

/**
 * Custom Vulkan drivers for Adreno GPUs (Turnip and newer Qualcomm builds, loaded through adrenotools): the ones
 * installed, a zip from the files, and the latest releases of the people who build them, downloaded from GitHub.
 */
@Composable
fun DriversScreen(vm: AppViewModel) {
    val list by vm.gpuDrivers.collectAsStateWithLifecycle()
    val chosen by vm.gpuDriver.collectAsStateWithLifecycle()
    val gpu by vm.gpuName.collectAsStateWithLifecycle()
    val lists by vm.driverLists.collectAsStateWithLifecycle()
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let { vm.importDriver(it) } }
    var sourceIndex by rememberSaveable { mutableIntStateOf(0) }
    val source = DriverSource.entries[sourceIndex]
    var confirmDelete by remember { mutableStateOf<com.dfdx047.dragonrage.data.GpuDriver?>(null) }
    val uri = LocalUriHandler.current

    LaunchedEffect(source) { vm.loadDrivers(source) }

    Column(
        Modifier.verticalScroll(rememberScrollState()).padding(horizontal = 16.dp).padding(bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        ScreenTitle(stringResource(R.string.nav_drivers), stringResource(R.string.drivers_sub))

        if (gpu.isNotEmpty() && !gpu.contains("Adreno", ignoreCase = true)) {
            SectionCard(stringResource(R.string.drivers_not_adreno_title), Icons.Rounded.Warning, accent = MaterialTheme.colorScheme.error) {
                Text(
                    stringResource(R.string.drivers_not_adreno, gpu),
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        SectionCard(stringResource(R.string.drivers_installed), Icons.Rounded.DeveloperBoard, accent = Dbz.KiBlue) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                RadioButton(selected = chosen == null, onClick = { vm.selectDriver(null) })
                Text(stringResource(R.string.gpu_driver_system), style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            }
            list.forEach { d ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    RadioButton(selected = chosen == d.id, onClick = { vm.selectDriver(d.id) })
                    Column(Modifier.weight(1f)) {
                        Text(d.name, style = MaterialTheme.typography.bodyLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        if (d.version.isNotEmpty()) {
                            Text(d.version, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    IconButton(onClick = { confirmDelete = d }) { Icon(Icons.Rounded.Delete, stringResource(R.string.remove)) }
                }
            }
            Text(stringResource(R.string.gpu_driver_text), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            FilledTonalButton(onClick = { pick.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream")) }) {
                Icon(Icons.Rounded.Upload, null, Modifier.size(18.dp)); Spacer(Modifier.size(6.dp)); Text(stringResource(R.string.gpu_driver_import))
            }
        }

        SectionCard(stringResource(R.string.drivers_download), Icons.Rounded.CloudDownload, accent = Dbz.SaiyanGold) {
            Text(stringResource(R.string.drivers_download_text), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            ChipRow(
                options = DriverSource.entries.map { it.label to it.ordinal },
                selected = sourceIndex,
                onSelect = { sourceIndex = it },
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(source.repo, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
                IconButton(onClick = { vm.loadDrivers(source, again = true) }) { Icon(Icons.Rounded.Refresh, stringResource(R.string.drivers_refresh)) }
                IconButton(onClick = { runCatching { uri.openUri(source.page) } }) { Icon(Icons.AutoMirrored.Rounded.OpenInNew, stringResource(R.string.drivers_open_page)) }
            }
            when (val l = lists[source]) {
                null, AppViewModel.DriverList.Loading -> Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.size(10.dp))
                    Text(stringResource(R.string.drivers_loading), style = MaterialTheme.typography.bodyMedium)
                }
                AppViewModel.DriverList.Failed -> Text(
                    stringResource(R.string.drivers_failed), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.error,
                )
                is AppViewModel.DriverList.Loaded -> {
                    if (l.releases.isEmpty()) Text(stringResource(R.string.drivers_none), style = MaterialTheme.typography.bodyMedium)
                    l.releases.forEachIndexed { i, r ->
                        if (i > 0) HorizontalDivider()
                        Text(r.title, style = MaterialTheme.typography.titleSmall)
                        Text("${r.tag} · ${r.date}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        r.assets.forEach { a ->
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Column(Modifier.weight(1f)) {
                                    Text(a.name, style = MaterialTheme.typography.bodyMedium, maxLines = 2, overflow = TextOverflow.Ellipsis)
                                    if (a.size > 0) Text(formatBytes(a.size), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                }
                                TextButton(onClick = { vm.downloadDriver(a) }) {
                                    Icon(Icons.Rounded.Download, null, Modifier.size(18.dp)); Spacer(Modifier.size(4.dp)); Text(stringResource(R.string.drivers_install))
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    confirmDelete?.let { d ->
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { confirmDelete = null },
            title = { Text(stringResource(R.string.drivers_remove_q)) },
            text = { Text(d.name) },
            confirmButton = { TextButton(onClick = { confirmDelete = null; vm.deleteDriver(d) }) { Text(stringResource(R.string.remove)) } },
            dismissButton = { TextButton(onClick = { confirmDelete = null }) { Text(stringResource(R.string.cancel)) } },
        )
    }
}
