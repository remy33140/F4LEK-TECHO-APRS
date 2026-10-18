package fr.f4lek.techoaprs.ui

import fr.f4lek.techoaprs.R
import fr.f4lek.techoaprs.L
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.MyLocation
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import fr.f4lek.techoaprs.ble.LinkState
import fr.f4lek.techoaprs.data.MsgState
import fr.f4lek.techoaprs.data.Summit
import kotlinx.coroutines.launch

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SotaScreen(onConnect: () -> Unit) {
    val techo = LocalTecho.current
    val snack = LocalSnack.current
    val scope = rememberCoroutineScope()
    val vm: SotaViewModel = viewModel { SotaViewModel(techo) }
    val link by techo.link.state.collectAsState()
    val msgVersion by techo.messages.version.collectAsState()
    val say: (String) -> Unit = { m -> scope.launch { snack.showSnackbar(m) } }

    LazyColumn(
        Modifier.fillMaxWidth(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        vm.loading?.let { msg ->
            item {
                Column {
                    Text(msg, style = MaterialTheme.typography.bodySmall)
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                }
            }
        }

        // ---------------------------------------------------------------- summit
        item {
            Section(L.s(R.string.sota_summit)) {
                OutlinedTextField(
                    value = vm.query,
                    onValueChange = vm::onQuery,
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    leadingIcon = { Icon(Icons.Filled.Search, null) },
                    trailingIcon = {
                        if (vm.query.isNotEmpty()) IconButton(onClick = { vm.onQuery("") }) { Icon(Icons.Filled.Clear, L.s(R.string.clear)) }
                    },
                    placeholder = { Text(L.s(R.string.sota_search_hint)) },
                    keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None),
                )
                if (vm.suggestions.isNotEmpty()) {
                    Column(Modifier.heightIn(max = 360.dp)) {
                        LazyColumn {
                            items(vm.suggestions, key = { it.code }) { s ->
                                SummitRow(s) { vm.pick(s) }
                                HorizontalDivider()
                            }
                        }
                    }
                } else if (vm.query.length >= 2) {
                    Text(L.s(R.string.sota_no_result), style = MaterialTheme.typography.bodySmall)
                }

                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Picker(
                        label = L.s(R.string.sota_assoc),
                        value = vm.assoc?.code ?: "",
                        options = vm.assocs,
                        text = { "${it.code} — ${it.name}" },
                        onPick = vm::chooseAssoc,
                        modifier = Modifier.weight(0.9f),
                    )
                    Picker(
                        label = L.s(R.string.sota_region),
                        value = vm.region?.code ?: "",
                        options = vm.regions,
                        text = { "${it.code} — ${it.name}" },
                        onPick = vm::chooseRegion,
                        modifier = Modifier.weight(1f),
                        enabled = vm.assoc != null,
                    )
                }
                Spacer(Modifier.height(8.dp))
                Picker(
                    label = L.s(R.string.sota_summit),
                    value = vm.summit?.takeIf { it.region == vm.region?.code && it.assoc == vm.assoc?.code }
                        ?.let { "${it.code.substringAfterLast('-')} ${it.name}" } ?: "",
                    options = vm.regionSummits,
                    text = { "${it.code.substringAfterLast('-')}  ${it.name}  (${it.altM} m)" },
                    onPick = { vm.pick(it) },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = vm.region != null,
                )

                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedButton(
                        onClick = { if (link is LinkState.Ready) vm.findNearby(say) else onConnect() },
                    ) {
                        Icon(Icons.Filled.MyLocation, null, Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(L.s(R.string.sota_near))
                    }
                    Spacer(Modifier.width(8.dp))
                    Text(L.s(R.string.sota_gps), style = MaterialTheme.typography.bodySmall)
                }
                vm.nearby?.let { list ->
                    Spacer(Modifier.height(4.dp))
                    for (s in list.take(10)) {
                        SummitRow(s) { vm.pick(s) }
                        HorizontalDivider()
                    }
                }
                if (vm.recents.isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    Text(L.s(R.string.sota_recent), style = MaterialTheme.typography.labelMedium)
                    LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        items(vm.recents, key = { it.code }) { s ->
                            FilterChip(
                                selected = vm.summit?.code == s.code,
                                onClick = { vm.pick(s) },
                                label = { Text(s.code) },
                            )
                        }
                    }
                }
                vm.summit?.let { s ->
                    Spacer(Modifier.height(12.dp))
                    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)) {
                        Column(Modifier.fillMaxWidth().padding(12.dp)) {
                            Text(s.code, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
                            Text(s.name, style = MaterialTheme.typography.titleMedium)
                            Text(
                                "${s.altM} m · " + L.s(R.string.sota_points, s.points) +
                                    (if (s.bonus > 0) L.s(R.string.sota_bonus, s.bonus) else "") +
                                    " · " + L.q(R.plurals.sota_activations, s.activations),
                                style = MaterialTheme.typography.bodyMedium,
                            )
                            if (s.regionName.isNotBlank()) Text(s.regionName, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------------- frequency & mode
        item {
            Section(L.s(R.string.sota_freq_mode)) {
                OutlinedTextField(
                    value = vm.freq,
                    onValueChange = { vm.freq = it },
                    label = { Text(L.s(R.string.sota_freq)) },
                    suffix = { Text("MHz") },
                    singleLine = true,
                    isError = vm.normalizedFreq() == null,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth(),
                )
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    for ((f, m) in FREQ_PRESETS) FilterChip(
                        selected = vm.normalizedFreq() == f.toDoubleOrNull()?.let { String.format(java.util.Locale.US, "%.3f", it) },
                        onClick = { vm.applyPreset(f, m) },
                        label = { Text(f) },
                    )
                }
                Spacer(Modifier.height(4.dp))
                Text(L.s(R.string.sota_mode), style = MaterialTheme.typography.labelMedium)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    for (m in SOTA_MODES) FilterChip(selected = vm.mode == m, onClick = { vm.mode = m }, label = { Text(m) })
                }
            }
        }

        // ---------------------------------------------------------------- callsign & comment
        item {
            Section(L.s(R.string.sota_call_comment)) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(
                        value = vm.prefix,
                        onValueChange = { vm.prefix = it.uppercase().take(4) },
                        label = { Text(L.s(R.string.sota_prefix)) },
                        placeholder = { Text("EA2") },
                        singleLine = true,
                        modifier = Modifier.weight(0.35f),
                        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
                    )
                    OutlinedTextField(
                        value = vm.baseCall,
                        onValueChange = { vm.baseCall = it.uppercase().take(16) },
                        label = { Text(L.s(R.string.sota_call)) },
                        placeholder = { Text("F4LEK/P") },
                        singleLine = true,
                        modifier = Modifier.weight(0.65f),
                        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
                    )
                }
                Text(
                    L.s(R.string.sota_prefix_hint, vm.callsign.ifBlank { "?" }),
                    style = MaterialTheme.typography.bodySmall,
                )
                Spacer(Modifier.height(4.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    for (c in SOTA_COMMENTS) FilterChip(
                        selected = vm.comment.trim().equals(c, ignoreCase = true),
                        onClick = { vm.comment = if (vm.comment.trim() == c) "" else c },
                        label = { Text(c) },
                    )
                }
                OutlinedTextField(
                    value = vm.comment,
                    onValueChange = { vm.comment = it.take(40) },
                    label = { Text(L.s(R.string.sota_comment)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        // ---------------------------------------------------------------- send
        item {
            val msg = vm.message()
            Section(L.s(R.string.sota_send_section)) {
                Text(L.s(R.string.sota_to), style = MaterialTheme.typography.labelMedium)
                Text(
                    msg ?: L.s(R.string.sota_incomplete),
                    fontFamily = FontFamily.Monospace,
                    style = MaterialTheme.typography.bodyLarge,
                    color = if (msg == null) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onSurface,
                )
                msg?.let {
                    Text(
                        L.s(R.string.sota_chars, it.length),
                        style = MaterialTheme.typography.bodySmall,
                        color = if (it.length > 67) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Spacer(Modifier.height(8.dp))
                Button(
                    onClick = { if (link is LinkState.Ready) vm.sendSpot(say) else onConnect() },
                    enabled = msg != null && !vm.busy,
                    modifier = Modifier.fillMaxWidth().height(52.dp),
                ) {
                    if (vm.busy) CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp)
                    else Icon(Icons.AutoMirrored.Filled.Send, null)
                    Spacer(Modifier.width(8.dp))
                    Text(if (link is LinkState.Ready) L.s(R.string.sota_send) else L.s(R.string.sota_connect))
                }
                OutlinedButton(
                    onClick = { vm.savePlanOnNode { _, m -> say(m) } },
                    enabled = vm.summit != null && techo.link.hasConfigChannel,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Icon(Icons.Filled.Save, null, Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(L.s(R.string.sota_save_plan))
                }
                val st = remember(msgVersion, vm.track) { vm.spotStatus() }
                st?.let { (state, answer) ->
                    Spacer(Modifier.height(8.dp))
                    Text(
                        when (state) {
                            MsgState.SENDING -> L.s(R.string.sota_st_sending)
                            MsgState.SENT -> L.s(R.string.sota_st_sent)
                            MsgState.ACKED -> L.s(R.string.sota_st_acked)
                            MsgState.REJECTED -> L.s(R.string.sota_st_rejected)
                            MsgState.FAILED -> L.s(R.string.sota_st_noack)
                            MsgState.RECEIVED -> ""
                        },
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    answer?.let { Text(L.s(R.string.sota_answer, it), fontFamily = FontFamily.Monospace) }
                }
            }
        }

        item {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(vm.listInfo, style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
                TextButton(onClick = { vm.refreshList(say) }, enabled = vm.loading == null) { Text(L.s(R.string.sota_update_list)) }
            }
        }
    }
}

@Composable
fun Section(title: String, content: @Composable () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
            content()
        }
    }
}

@Composable
private fun SummitRow(s: Summit, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 8.dp, horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Row {
                Text(s.code, fontWeight = FontWeight.Bold)
                Text("  ${s.name}", maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            Text(
                "${s.altM} m · " + L.s(R.string.sota_points, s.points) + (if (s.regionName.isNotBlank()) " · ${s.regionName}" else ""),
                style = MaterialTheme.typography.bodySmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        s.distanceKm?.let { Text(String.format(java.util.Locale.getDefault(), "%.1f km", it), style = MaterialTheme.typography.bodySmall) }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun <T> Picker(
    label: String,
    value: String,
    options: List<T>,
    text: (T) -> String,
    onPick: (T) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    var open by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(expanded = open && enabled, onExpandedChange = { if (enabled) open = it }, modifier = modifier) {
        OutlinedTextField(
            value = value,
            onValueChange = {},
            readOnly = true,
            enabled = enabled,
            singleLine = true,
            label = { Text(label) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = open) },
            modifier = Modifier.menuAnchor(MenuAnchorType.PrimaryNotEditable, enabled).fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = open && enabled, onDismissRequest = { open = false }) {
            for (o in options) DropdownMenuItem(text = { Text(text(o)) }, onClick = { onPick(o); open = false })
        }
    }
}
