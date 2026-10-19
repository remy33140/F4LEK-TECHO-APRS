package fr.f4lek.techoaprs.ui

import fr.f4lek.techoaprs.net.IsState
import fr.f4lek.techoaprs.net.AprsIsLink
import fr.f4lek.techoaprs.Source
import fr.f4lek.techoaprs.LinkService
import androidx.compose.material3.ProvideTextStyle
import fr.f4lek.techoaprs.TechoApp
import androidx.compose.ui.platform.LocalContext
import android.app.Activity
import fr.f4lek.techoaprs.R
import fr.f4lek.techoaprs.L
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.compose.viewModel
import fr.f4lek.techoaprs.Techo
import fr.f4lek.techoaprs.ble.LinkState
import fr.f4lek.techoaprs.node.NodeClient
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

/** Form state: edited values only, as text for the number fields so typing stays free. */
class ConfigViewModel(private val techo: Techo) : ViewModel() {
    val edits = mutableStateMapOf<String, Any>()
    var saving by mutableStateOf(false)
        private set

    fun current(cfg: JSONObject, key: String): Any? = edits[key] ?: cfg.opt(key)

    fun text(cfg: JSONObject, key: String): String = when (val v = current(cfg, key)) {
        null, JSONObject.NULL -> ""
        is Double, is Float -> fmt(v as Number)
        else -> v.toString()
    }

    fun profiles(cfg: JSONObject): JSONArray = (edits["profiles"] as? JSONArray) ?: cfg.optJSONArray("profiles") ?: JSONArray()

    fun setProfile(cfg: JSONObject, idx: Int, key: String, value: Any) {
        val arr = JSONArray(profiles(cfg).toString())
        arr.getJSONObject(idx).put(key, value)
        edits["profiles"] = arr
    }

    fun validate(cfg: JSONObject): Map<String, String> {
        val err = mutableMapOf<String, String>()
        for (sec in CONFIG_SCHEMA) for (f in sec.fields) {
            val v = edits[f.key] ?: continue
            when (f) {
                is Field.IntF -> {
                    val n = v.toString().trim().toIntOrNull()
                    if (n == null || n !in f.min..f.max) err[f.key] = L.s(R.string.cfg_between, "${f.min}", "${f.max}")
                    else if ((f.key == "telemetryIntervalMin" || f.key == "wxIntervalMin") && n in 1..14) err[f.key] = L.s(R.string.cfg_0_or_15)
                }
                is Field.FloatF -> {
                    val n = v.toString().replace(',', '.').trim().toDoubleOrNull()
                    if (n == null || n < f.min || n > f.max) err[f.key] = L.s(R.string.cfg_between, fmt(f.min), fmt(f.max))
                }
                is Field.Text -> if (f.key == "blePin" && !Regex("^[0-9]{6}$").matches(v.toString())) err[f.key] = L.s(R.string.cfg_6digits)
                    else if (f.key == "callsign" && v.toString().isBlank()) err[f.key] = L.s(R.string.cfg_required)
                else -> {}
            }
        }
        for (k in listOf("latitude", "longitude")) edits[k]?.let {
            val n = it.toString().replace(',', '.').trim().toDoubleOrNull()
            val lim = if (k == "latitude") 90.0 else 180.0
            if (n == null || n < -lim || n > lim) err[k] = L.s(R.string.cfg_between, "-${lim.toInt()}", "${lim.toInt()}")
        }
        val ssids = (1..3).map { profiles(cfg).optJSONObject(it)?.optInt("ssid") ?: 0 }.filter { it != 0 }
        if (ssids.size != ssids.toSet().size) err["profiles"] = L.s(R.string.cfg_dup_ssid)
        return err
    }

    /** Only what really changed, typed the way the firmware expects. */
    fun changes(cfg: JSONObject): JSONObject {
        val out = JSONObject()
        val types = CONFIG_SCHEMA.flatMap { it.fields }.associateBy { it.key }
        for ((k, raw) in edits) {
            val v: Any = when (val f = types[k]) {
                is Field.IntF -> raw.toString().trim().toInt()
                is Field.FloatF -> raw.toString().replace(',', '.').trim().toDouble()
                is Field.Text -> if (f.upper) raw.toString().trim().uppercase() else raw.toString()
                else -> when (k) {
                    "latitude", "longitude" -> raw.toString().replace(',', '.').trim().toDouble()
                    else -> raw
                }
            }
            if (!NodeClient.same(v, cfg.opt(k))) out.put(k, v)
        }
        return out
    }

    fun save(cfg: JSONObject, say: (String) -> Unit) {
        val err = validate(cfg)
        if (err.isNotEmpty()) return say(L.s(R.string.cfg_fix, err.keys.joinToString()))
        val ch = changes(cfg)
        if (ch.length() == 0) { edits.clear(); return say(L.s(R.string.cfg_nothing)) }
        val needReboot = CONFIG_SCHEMA.flatMap { it.fields }.any { it.reboot && ch.has(it.key) } || ch.has("blePin")
        saving = true
        viewModelScope.launch {
            val r = runCatching { techo.node.setConfig(ch) }
            saving = false
            r.onSuccess {
                edits.clear()
                say(if (needReboot) L.s(R.string.cfg_saved_reboot) else L.s(R.string.cfg_saved))
            }.onFailure { say(L.s(R.string.cfg_refused, it.message ?: "")) }
        }
    }

    fun run(say: (String) -> Unit, block: suspend () -> String) {
        viewModelScope.launch { say(runCatching { block() }.getOrElse { L.s(R.string.failed, it.message ?: "") }) }
    }

    companion object {
        fun fmt(n: Number): String {
            val d = n.toDouble()
            return if (d == Math.floor(d) && Math.abs(d) < 1e9) d.toLong().toString()
            else String.format(Locale.US, "%.6f", d).trimEnd('0').trimEnd('.')
        }
    }
}

@Composable
fun ConfigScreen(onConnect: () -> Unit) {
    val techo = LocalTecho.current
    val snack = LocalSnack.current
    val scope = rememberCoroutineScope()
    val say: (String) -> Unit = { m -> scope.launch { snack.showSnackbar(m) } }
    val vm: ConfigViewModel = viewModel { ConfigViewModel(techo) }
    val link by techo.link.state.collectAsState()
    val cfg by techo.node.config.collectAsState()
    val status by techo.node.status.collectAsState()
    var confirm by remember { mutableStateOf<String?>(null) }
    var console by rememberSaveable { mutableStateOf(false) }

    // Web mode: no T-Echo, only the app settings and the APRS-IS identity.
    if (techo.isWeb) {
        WebSettings(say)
        return
    }
    if (link !is LinkState.Ready) {
        Column(Modifier.fillMaxWidth().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(L.s(R.string.cfg_connect_prompt), style = MaterialTheme.typography.bodyLarge)
            Spacer(Modifier.height(12.dp))
            Button(onClick = onConnect) { Text(L.s(R.string.connect)) }
            Spacer(Modifier.height(24.dp))
            LanguageCard()
        }
        return
    }
    if (!techo.link.hasConfigChannel) {
        Text(
            L.s(R.string.cfg_kiss_only),
            Modifier.padding(24.dp),
        )
        Column(Modifier.padding(horizontal = 12.dp)) { LanguageCard() }
        return
    }
    val c = cfg
    if (c == null) {
        LaunchedEffect(Unit) { runCatching { techo.node.refreshConfig() }.onFailure { say(it.message ?: L.s(R.string.error)) } }
        Text(L.s(R.string.cfg_reading), Modifier.padding(24.dp))
        return
    }
    val errors = vm.validate(c)
    val dirty = vm.edits.isNotEmpty()

    Box(Modifier.fillMaxSize()) {
    LazyColumn(
        contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item { LanguageCard() }
        item { StatusCard(status, onRefresh = { vm.run(say) { techo.node.refreshStatus(); L.s(R.string.cfg_status_updated) } }) }
        item {
            Section(L.s(R.string.cfg_actions)) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = { vm.run(say) { techo.node.beacon() } }) { Text(L.s(R.string.cfg_beacon_now)) }
                    OutlinedButton(onClick = { vm.edits.clear(); vm.run(say) { techo.node.refreshConfig(); L.s(R.string.cfg_reread_done) } }) {
                        Text(L.s(R.string.cfg_reread))
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = { confirm = "reboot" }) { Text(L.s(R.string.cfg_reboot)) }
                    OutlinedButton(onClick = { confirm = "factory" }) { Text(L.s(R.string.cfg_factory)) }
                }
                TextButton(onClick = { console = !console }) { Text(if (console) L.s(R.string.cfg_console_hide) else L.s(R.string.cfg_console_show)) }
                if (console) Console()
            }
        }
        items(CONFIG_SCHEMA, key = { it.title }) { sec ->
            ConfigSection(sec, c, vm, errors, status)
        }
        item { Spacer(Modifier.height(72.dp)) }
    }
    if (dirty) SaveBar(
        vm.saving, errors.isEmpty(), onSave = { vm.save(c, say) }, onCancel = { vm.edits.clear() },
        modifier = Modifier.align(Alignment.BottomCenter),
    )
    }

    confirm?.let { what ->
        AlertDialog(
            onDismissRequest = { confirm = null },
            title = { Text(if (what == "reboot") L.s(R.string.cfg_reboot_q) else L.s(R.string.cfg_factory_q)) },
            text = {
                Text(
                    if (what == "reboot") L.s(R.string.cfg_reboot_text) else L.s(R.string.cfg_factory_text)
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirm = null
                    if (what == "reboot") vm.run(say) { techo.node.reboot(); L.s(R.string.cfg_rebooting) }
                    else vm.run(say) { techo.node.factoryReset(); L.s(R.string.cfg_factory_done) }
                }) { Text(L.s(R.string.confirm)) }
            },
            dismissButton = { TextButton(onClick = { confirm = null }) { Text(L.s(R.string.cancel)) } },
        )
    }
}

/** App language: English, French or the phone's. Applied at once (the activity is recreated). */
@Composable
private fun LanguageCard() {
    val techo = LocalTecho.current
    val ctx = LocalContext.current
    val name = { l: String ->
        when (l) {
            "fr" -> L.s(R.string.lang_fr)
            L.SYSTEM -> L.s(R.string.lang_system)
            else -> L.s(R.string.lang_en)
        }
    }
    Section(L.s(R.string.app_section)) {
        Picker(
            label = L.s(R.string.language),
            value = name(L.lang(ctx)),
            options = L.LANGS,
            text = name,
            onPick = { lang ->
                if (lang != L.lang(ctx)) {
                    (ctx.applicationContext as TechoApp).setLanguage(lang)
                    (ctx as? Activity)?.recreate()
                }
            },
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(8.dp))
        val source by techo.source.collectAsState()
        val srcName = { s: Source -> L.s(if (s == Source.WEB) R.string.src_web else R.string.src_techo) }
        Picker(
            label = L.s(R.string.src_label),
            value = srcName(source),
            options = Source.entries.toList(),
            text = srcName,
            onPick = { s ->
                if (s != source) {
                    techo.setSource(s)
                    (ctx as? Activity)?.recreate() // tabs, start screen and permissions follow the source
                }
            },
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

/** Web mode settings: app card, what the mode does, and the APRS-IS identity. */
@Composable
private fun WebSettings(say: (String) -> Unit) {
    val techo = LocalTecho.current
    val state by techo.aprsIs.state.collectAsState()
    var call by rememberSaveable { mutableStateOf(techo.webCall) }
    var pass by rememberSaveable { mutableStateOf(techo.prefs.getString("web.pass", "") ?: "") }
    var server by rememberSaveable { mutableStateOf(techo.webServer) }
    val callOk = AprsIsLink.validCall(call)
    LazyColumn(
        contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item { LanguageCard() }
        item {
            Text(L.s(R.string.web_mode_note), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(horizontal = 4.dp))
        }
        item {
            Section(L.s(R.string.web_section)) {
                OutlinedTextField(
                    value = call,
                    onValueChange = { call = it.uppercase().filter { c -> c.isLetterOrDigit() || c == '-' }.take(9) },
                    label = { Text(L.s(R.string.web_call)) },
                    singleLine = true,
                    isError = !callOk,
                    supportingText = { Text(L.s(if (callOk || call.isBlank()) R.string.web_call_hint else R.string.web_bad_call)) },
                    keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = pass,
                    onValueChange = { pass = it.filter { c -> c.isDigit() }.take(5) },
                    label = { Text(L.s(R.string.web_pass)) },
                    singleLine = true,
                    supportingText = { Text(L.s(R.string.web_pass_hint, AprsIsLink.passcode(call).toString())) },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = server,
                    onValueChange = { server = it.trim() },
                    label = { Text(L.s(R.string.web_server)) },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                    modifier = Modifier.fillMaxWidth(),
                )
                Button(
                    onClick = { techo.setWebIdentity(call, pass, server); say(L.s(R.string.web_saved)) },
                    enabled = callOk,
                ) { Text(L.s(R.string.web_apply)) }
                Spacer(Modifier.height(4.dp))
                Text(
                    if (!AprsIsLink.validCall(techo.webCall)) L.s(R.string.web_need_call) else LinkService.webStateText(state),
                    style = MaterialTheme.typography.bodyMedium,
                    color = when {
                        state is IsState.Connected && (state as IsState.Connected).verified -> MaterialTheme.colorScheme.primary
                        state is IsState.Failed || state is IsState.Connected -> MaterialTheme.colorScheme.error
                        else -> MaterialTheme.colorScheme.onSurfaceVariant
                    },
                )
                Text(L.s(R.string.web_note), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun SaveBar(saving: Boolean, valid: Boolean, onSave: () -> Unit, onCancel: () -> Unit, modifier: Modifier) {
        Card(
            modifier.fillMaxWidth().padding(12.dp),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
            elevation = CardDefaults.cardElevation(6.dp),
        ) {
            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(if (valid) L.s(R.string.cfg_unsaved) else L.s(R.string.cfg_invalid), Modifier.weight(1f))
                TextButton(onClick = onCancel) { Text(L.s(R.string.cancel)) }
                Button(onClick = onSave, enabled = !saving && valid) { Text(if (saving) "…" else L.s(R.string.save)) }
            }
        }
}

@Composable
private fun StatusCard(st: JSONObject?, onRefresh: () -> Unit) {
    Section(L.s(R.string.cfg_status)) {
        if (st == null) {
            Text("—")
        } else {
            val sens = st.optJSONObject("sensors")
            val gps = st.optJSONObject("gps")
            val radio = st.optJSONObject("radio")
            val vbat = sens?.optDouble("inaBusV")?.takeIf { it in 2.0..5.0 } ?: sens?.optDouble("vbatDivV")
            Text("${st.optString("version")} ${st.optString("build")} · ${st.optString("board")}", style = MaterialTheme.typography.bodySmall)
            vbat?.takeIf { !it.isNaN() }?.let { Text(L.s(R.string.cfg_battery, it)) }
            if (gps != null) Text(
                if (gps.optBoolean("fix")) L.s(
                    R.string.cfg_gps_fix,
                    gps.optDouble("lat"), gps.optDouble("lon"), gps.optInt("altM"), gps.optInt("sats")
                ) else L.s(R.string.cfg_gps_nofix, gps.optInt("inView"))
            )
            if (radio != null) Text(
                L.s(R.string.cfg_radio, radio.optString("module"), radio.optString("state"), radio.optInt("rxCount"), radio.optInt("txCount")) +
                    if (radio.has("lastRssi")) L.s(R.string.cfg_last_rssi, radio.optDouble("lastRssi")) else ""
            )
            sens?.takeIf { it.optBoolean("wx") }?.let {
                Text(L.s(R.string.cfg_weather, it.optDouble("tempC"), it.optDouble("hum"), it.optDouble("hPa")))
            }
        }
        TextButton(onClick = onRefresh) { Text(L.s(R.string.refresh)) }
    }
}

@Composable
private fun ConfigSection(sec: SectionDef, cfg: JSONObject, vm: ConfigViewModel, errors: Map<String, String>, status: JSONObject?) {
    var open by rememberSaveable(sec.title) { mutableStateOf(sec.title == R.string.s_station) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Row(Modifier.fillMaxWidth().clickable { open = !open }, verticalAlignment = Alignment.CenterVertically) {
                Text(L.s(sec.title), style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary, modifier = Modifier.weight(1f))
                val changed = sec.fields.count { vm.edits.containsKey(it.key) || (it is Field.Position && vm.edits.containsKey("longitude")) }
                if (changed > 0) Text(L.s(R.string.cfg_changed, changed) + "  ", style = MaterialTheme.typography.labelSmall)
                Icon(if (open) Icons.Filled.ExpandLess else Icons.Filled.ExpandMore, null)
            }
            if (open) for (f in sec.fields) {
                Spacer(Modifier.height(8.dp))
                FieldEditor(f, cfg, vm, errors, status)
            }
        }
    }
}

@Composable
private fun FieldEditor(f: Field, cfg: JSONObject, vm: ConfigViewModel, errors: Map<String, String>, status: JSONObject?) {
    val err = errors[f.key]
    val supporting: (@Composable () -> Unit)? = (err ?: (f.hint?.let { h -> if (f.reboot) L.s(h) + " · " + L.s(R.string.cfg_reboot_needed) else L.s(h) }
        ?: if (f.reboot) L.s(R.string.cfg_reboot_needed) else null))?.let { t -> { Text(t) } }
    when (f) {
        is Field.Text -> OutlinedTextField(
            value = vm.text(cfg, f.key),
            onValueChange = { vm.edits[f.key] = (if (f.upper) it.uppercase() else it).take(f.maxLen) },
            label = { Text(L.s(f.label)) },
            singleLine = true,
            isError = err != null,
            supportingText = supporting,
            keyboardOptions = KeyboardOptions(
                capitalization = if (f.upper) KeyboardCapitalization.Characters else KeyboardCapitalization.Sentences,
                keyboardType = if (f.key == "blePin") KeyboardType.NumberPassword else KeyboardType.Text,
            ),
            modifier = Modifier.fillMaxWidth(),
        )
        is Field.IntF -> OutlinedTextField(
            value = vm.text(cfg, f.key),
            onValueChange = { vm.edits[f.key] = it.filter { ch -> ch.isDigit() || ch == '-' } },
            label = { Text(L.s(f.label)) },
            singleLine = true,
            isError = err != null,
            supportingText = supporting,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier.fillMaxWidth(),
        )
        is Field.FloatF -> OutlinedTextField(
            value = vm.text(cfg, f.key),
            onValueChange = { vm.edits[f.key] = it },
            label = { Text(L.s(f.label)) },
            singleLine = true,
            isError = err != null,
            supportingText = supporting,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
            modifier = Modifier.fillMaxWidth(),
        )
        is Field.Bool -> Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(L.s(f.label))
                f.hint?.let { Text(L.s(it), style = MaterialTheme.typography.bodySmall) }
            }
            Switch(checked = vm.current(cfg, f.key) as? Boolean ?: false, onCheckedChange = { vm.edits[f.key] = it })
        }
        is Field.Choice -> {
            val cur = vm.current(cfg, f.key) as? Number
            Picker(
                label = L.s(f.label),
                value = f.options.firstOrNull { cur != null && NodeClient.same(it.first, cur) }?.second?.let { optionText(it) } ?: cur?.toString() ?: "",
                options = f.options,
                text = { optionText(it.second) },
                onPick = { vm.edits[f.key] = it.first },
                modifier = Modifier.fillMaxWidth(),
            )
            supporting?.let {
                Row(Modifier.padding(start = 16.dp, top = 4.dp)) {
                    ProvideTextStyle(MaterialTheme.typography.bodySmall.copy(color = MaterialTheme.colorScheme.onSurfaceVariant)) { it() }
                }
            }
        }
        is Field.Position -> {
            Text(L.s(f.label), style = MaterialTheme.typography.labelLarge)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (k in listOf("latitude", "longitude")) OutlinedTextField(
                    value = vm.text(cfg, k),
                    onValueChange = { vm.edits[k] = it },
                    label = { Text(L.s(if (k == "latitude") R.string.latitude else R.string.longitude)) },
                    singleLine = true,
                    isError = errors[k] != null,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.weight(1f),
                )
            }
            val gps = status?.optJSONObject("gps")
            if (gps?.optBoolean("fix") == true) TextButton(onClick = {
                vm.edits["latitude"] = String.format(Locale.US, "%.5f", gps.optDouble("lat"))
                vm.edits["longitude"] = String.format(Locale.US, "%.5f", gps.optDouble("lon"))
            }) { Text(L.s(R.string.cfg_take_gps)) }
        }
        is Field.Profiles -> ProfilesEditor(cfg, vm, err)
    }
}

@Composable
private fun ProfilesEditor(cfg: JSONObject, vm: ConfigViewModel, err: String?) {
    Text(L.s(R.string.f_profiles), style = MaterialTheme.typography.labelLarge)
    Text(L.s(R.string.cfg_profiles_hint), style = MaterialTheme.typography.bodySmall)
    err?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    val arr = vm.profiles(cfg)
    for (i in 0 until minOf(4, arr.length())) {
        val p = arr.getJSONObject(i)
        HorizontalDivider(Modifier.padding(vertical = 6.dp))
        Text(L.s(PROFILE_NAMES[i]), style = MaterialTheme.typography.titleSmall)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            SmallNum(L.s(R.string.prof_ssid), p.optInt("ssid"), 0..15, Modifier.weight(1f)) { vm.setProfile(cfg, i, "ssid", it) }
            OutlinedTextField(
                value = p.optString("symbol"),
                onValueChange = { s -> if (s.length <= 2) vm.setProfile(cfg, i, "symbol", s) },
                label = { Text(L.s(R.string.prof_symbol)) },
                singleLine = true,
                textStyle = MaterialTheme.typography.bodyLarge.copy(fontFamily = FontFamily.Monospace),
                modifier = Modifier.weight(1f),
            )
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            SmallNum(L.s(R.string.prof_slow), p.optInt("slowSec"), 0..3600, Modifier.weight(1f)) { vm.setProfile(cfg, i, "slowSec", it) }
            SmallNum(L.s(R.string.prof_fast), p.optInt("fastSec"), 0..3600, Modifier.weight(1f)) { vm.setProfile(cfg, i, "fastSec", it) }
            SmallNum(L.s(R.string.prof_dist), p.optInt("distM"), 0..5000, Modifier.weight(1f)) { vm.setProfile(cfg, i, "distM", it) }
        }
    }
}

@Composable
private fun SmallNum(label: String, value: Int, range: IntRange, modifier: Modifier, onChange: (Int) -> Unit) {
    var text by remember(value) { mutableStateOf(value.toString()) }
    OutlinedTextField(
        value = text,
        onValueChange = { s ->
            text = s.filter { it.isDigit() }.take(4)
            text.toIntOrNull()?.takeIf { it in range }?.let(onChange)
        },
        label = { Text(label) },
        singleLine = true,
        isError = text.toIntOrNull()?.let { it !in range } ?: true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        modifier = modifier,
    )
}

@Composable
private fun Console() {
    val techo = LocalTecho.current
    val scope = rememberCoroutineScope()
    val log by techo.node.log.collectAsState()
    val heard by techo.heard.collectAsState()
    var line by remember { mutableStateOf("") }
    Text(L.s(R.string.console_replies), style = MaterialTheme.typography.labelMedium)
    Column {
        for (l in log.takeLast(12)) Text(l, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall, maxLines = 3)
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(
            value = line, onValueChange = { line = it }, singleLine = true,
            placeholder = { Text("help, ble, set …") }, modifier = Modifier.weight(1f),
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None),
        )
        Spacer(Modifier.width(6.dp))
        Button(onClick = { val l = line; line = ""; scope.launch { techo.node.cli(l) } }, enabled = line.isNotBlank()) { Text("OK") }
    }
    Spacer(Modifier.height(8.dp))
    Text(L.s(R.string.console_heard), style = MaterialTheme.typography.labelMedium)
    for (h in heard.takeLast(10).reversed()) Text(h, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall, maxLines = 2)
}
