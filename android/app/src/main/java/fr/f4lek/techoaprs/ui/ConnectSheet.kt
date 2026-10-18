package fr.f4lek.techoaprs.ui

import fr.f4lek.techoaprs.R
import fr.f4lek.techoaprs.L
import android.content.Intent
import android.provider.Settings
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Router
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import fr.f4lek.techoaprs.ble.LinkState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConnectSheet(onDismiss: () -> Unit) {
    val techo = LocalTecho.current
    val ctx = LocalContext.current
    val state by techo.link.state.collectAsState()
    val found by techo.link.found.collectAsState()
    val last = techo.prefs.getString("lastDevice", null)

    LaunchedEffect(Unit) {
        if (state !is LinkState.Ready && state !is LinkState.Connecting && techo.link.bluetoothOn) techo.link.startScan()
    }

    ModalBottomSheet(onDismissRequest = { techo.link.stopScan(); onDismiss() }) {
        Column(Modifier.padding(horizontal = 16.dp).padding(bottom = 24.dp)) {
            Text("T-Echo", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(8.dp))
            when (val s = state) {
                is LinkState.Ready -> {
                    Text(L.s(R.string.cs_connected_to, s.name), style = MaterialTheme.typography.bodyLarge)
                    Text(
                        if (s.hasConfig) L.s(R.string.cs_kiss_cfg, s.mtu)
                        else L.s(R.string.cs_kiss_only),
                        style = MaterialTheme.typography.bodySmall,
                    )
                    if (!s.hasConfig) Text(
                        L.s(R.string.cs_services, s.services.joinToString { it.substring(0, 8) }),
                        style = MaterialTheme.typography.bodySmall,
                    )
                    Spacer(Modifier.height(12.dp))
                    OutlinedButton(onClick = { techo.disconnect() }) { Text(L.s(R.string.disconnect)) }
                }
                is LinkState.Connecting -> Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp)
                    Text("  ${s.step}")
                    Spacer(Modifier.weight(1f))
                    OutlinedButton(onClick = { techo.disconnect() }) { Text(L.s(R.string.cancel)) }
                }
                is LinkState.Failed -> Text(s.reason, color = MaterialTheme.colorScheme.error)
                else -> {}
            }
            if (!techo.link.bluetoothOn) {
                Text(L.s(R.string.cs_bt_off), color = MaterialTheme.colorScheme.error)
                Button(onClick = { ctx.startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS)) }) {
                    Text(L.s(R.string.cs_open_bt))
                }
            }
            if (state !is LinkState.Ready && state !is LinkState.Connecting) {
                Spacer(Modifier.height(12.dp))
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(L.s(R.string.cs_nearby), style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
                    if (state is LinkState.Scanning) CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                    else OutlinedButton(onClick = { techo.link.startScan() }) { Text(L.s(R.string.cs_search)) }
                }
                if (found.isEmpty() && state !is LinkState.Scanning) Text(
                    L.s(R.string.cs_none),
                    style = MaterialTheme.typography.bodySmall,
                )
                for (d in found) {
                    ListItem(
                        headlineContent = { Text(d.name) },
                        supportingContent = { Text("${d.address} · ${d.rssi} dBm" + if (d.address == last) L.s(R.string.cs_last_used) else "") },
                        leadingContent = { Icon(Icons.Filled.Router, null) },
                        modifier = Modifier.fillMaxWidth().clickable { techo.connect(d.address) },
                    )
                    HorizontalDivider()
                }
                if (last != null && found.none { it.address == last }) {
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(onClick = { techo.connect(last) }) { Text(L.s(R.string.cs_reconnect, last)) }
                }
                Spacer(Modifier.height(8.dp))
                Text(
                    L.s(R.string.cs_pin_hint),
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
    }
}
