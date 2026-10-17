package com.dfdx047.dragonrage.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.dfdx047.dragonrage.AppViewModel
import com.dfdx047.dragonrage.R
import com.dfdx047.dragonrage.ui.theme.Dbz

/** "We found your GPU: these settings keep it at 30 pictures a second" (GpuProfiles), with yes and not now. */
@Composable
fun GpuOfferDialog(vm: AppViewModel) {
    val p by vm.gpuOffer.collectAsStateWithLifecycle()
    val offer = p ?: return
    val items = buildList {
        add(stringResource(R.string.gpu_opt_resolution, if (offer.scale4 % 4 == 0) "${offer.scale4 / 4}x" else "${offer.scale4 / 4.0}x"))
        add(stringResource(if (offer.interp == 0) R.string.gpu_opt_interp_off else R.string.gpu_opt_interp_60))
        add(stringResource(if (offer.fragDepth == 0) R.string.gpu_opt_depth_off else R.string.gpu_opt_depth_on))
        add(stringResource(R.string.gpu_opt_frameskip))
        if (offer.fxOffAdd and 16 != 0) add(stringResource(R.string.gpu_opt_blur_far))
        if (offer.fxOffAdd and 32 != 0) add(stringResource(R.string.gpu_opt_blur_move))
        add(stringResource(R.string.gpu_opt_filter))
    }
    AlertDialog(
        onDismissRequest = { vm.declineGpuOffer() },
        icon = { Icon(Icons.Rounded.Memory, null, tint = Dbz.SaiyanGold) },
        title = { Text(stringResource(R.string.gpu_offer_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(stringResource(R.string.gpu_offer_found, offer.gpu, tierName(offer.tier)), style = MaterialTheme.typography.bodyMedium)
                Text(stringResource(R.string.gpu_offer_apply), style = MaterialTheme.typography.bodyMedium)
                items.forEach { Text("•  $it", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.primary) }
                Text(stringResource(R.string.gpu_offer_later), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        },
        confirmButton = { Button(onClick = { vm.acceptGpuOffer() }) { Text(stringResource(R.string.gpu_offer_yes)) } },
        dismissButton = { TextButton(onClick = { vm.declineGpuOffer() }) { Text(stringResource(R.string.gpu_offer_no)) } },
    )
}
