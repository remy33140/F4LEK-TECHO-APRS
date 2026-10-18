package fr.f4lek.techoaprs.ui

import fr.f4lek.techoaprs.R
import fr.f4lek.techoaprs.L
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import fr.f4lek.techoaprs.Techo
import fr.f4lek.techoaprs.data.MsgState
import fr.f4lek.techoaprs.data.Named
import fr.f4lek.techoaprs.data.Summit
import fr.f4lek.techoaprs.proto.Aprs
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.util.Locale

/** The 7 modes APRS2SOTA understands (firmware kSotaModes, sota.cpp). */
val SOTA_MODES = listOf("CW", "SSB", "FM", "DATA", "DV", "AM", "OTHER")
val SOTA_COMMENTS = listOf("QRV", "QSY", "QRT", "TEST")

/** Usual SOTA frequencies (MHz) with the mode that usually goes with them. */
val FREQ_PRESETS = listOf(
    "7.032" to "CW", "7.118" to "SSB", "10.118" to "CW", "14.062" to "CW", "14.285" to "SSB",
    "18.086" to "CW", "21.062" to "CW", "28.062" to "CW", "145.500" to "FM", "433.500" to "FM",
)

data class SpotTrack(val rowId: Long, val sentAt: Long, val msgId: String?)

class SotaViewModel(private val techo: Techo) : ViewModel() {
    private val db = techo.summits
    private val prefs = techo.prefs

    var loading by mutableStateOf<String?>(L.s(R.string.sota_loading))
        private set
    // Kept as data and formatted by the screen: this view model outlives a language change.
    var listCount by mutableStateOf(0)
        private set
    var listDate by mutableStateOf<String?>(null)
        private set

    // ------------------------------------------------ summit choice
    var query by mutableStateOf("")
        private set
    var suggestions by mutableStateOf<List<Summit>>(emptyList())
        private set
    var assocs by mutableStateOf<List<Named>>(emptyList())
        private set
    var assoc by mutableStateOf<Named?>(null)
        private set
    var regions by mutableStateOf<List<Named>>(emptyList())
        private set
    var region by mutableStateOf<Named?>(null)
        private set
    var regionSummits by mutableStateOf<List<Summit>>(emptyList())
        private set
    var summit by mutableStateOf<Summit?>(null)
        private set
    var nearby by mutableStateOf<List<Summit>?>(null)
        private set
    var recents by mutableStateOf<List<Summit>>(emptyList())
        private set

    // ------------------------------------------------ spot details
    // Saved as soon as they change: the callsign must still be there at the next launch.
    private var _freq by mutableStateOf(prefs.getString("sota.freq", "14.062")!!)
    var freq: String
        get() = _freq
        set(v) { _freq = v; prefs.edit().putString("sota.freq", v).apply() }
    private var _mode by mutableStateOf(prefs.getString("sota.mode", "CW")!!)
    var mode: String
        get() = _mode
        set(v) { _mode = v; prefs.edit().putString("sota.mode", v).apply() }
    private var _prefix by mutableStateOf(prefs.getString("sota.prefix", "")!!)
    var prefix: String
        get() = _prefix
        set(v) { _prefix = v; prefs.edit().putString("sota.prefix", v).apply() }
    private var _baseCall by mutableStateOf(prefs.getString("sota.call", "")!!)
    var baseCall: String
        get() = _baseCall
        set(v) { _baseCall = v; prefs.edit().putString("sota.call", v).apply() }
    var comment by mutableStateOf("")

    var busy by mutableStateOf(false)
        private set
    var track by mutableStateOf<SpotTrack?>(null)
        private set

    private var searchJob: Job? = null

    init {
        viewModelScope.launch(Dispatchers.IO) {
            val ok = runCatching {
                db.ensureLoaded { n -> loading = L.s(R.string.sota_importing, n) }
            }.onFailure { loading = L.s(R.string.sota_unreadable, it.message ?: "") }.isSuccess
            loadLists()
            prefs.getString("sota.summit", null)?.let { code -> db.byCode(code)?.let { withContext(Dispatchers.Main) { pick(it, remember = false) } } }
            if (ok) loading = null
        }
        // Prefill from the node: its callsign (as /P) and the activation plan saved on it.
        viewModelScope.launch {
            techo.node.config.collect { cfg -> cfg?.let { fromNode(it) } }
        }
    }

    private suspend fun loadLists() {
        val a = db.associations()
        val date = db.listDate()
        val n = db.count()
        val rec = (prefs.getString("sota.recents", "") ?: "").split(',').filter { it.isNotBlank() }.mapNotNull { db.byCode(it) }
        withContext(Dispatchers.Main) {
            assocs = a
            recents = rec
            listCount = n
            listDate = date
        }
    }

    private fun fromNode(cfg: JSONObject) {
        val call = cfg.optString("callsign")
        if (baseCall.isBlank() && call.isNotBlank() && !call.startsWith("NOCALL")) baseCall = Aprs.portableCall(call)
        val plan = cfg.optString("sotaPlan").split(',')
        if (summit == null && plan.size == 4) viewModelScope.launch(Dispatchers.IO) {
            val s = db.byCode(plan[0]) ?: return@launch
            withContext(Dispatchers.Main) {
                pick(s, remember = false)
                prefix = plan[1]
                plan[2].toIntOrNull()?.let { freq = String.format(Locale.US, "%.3f", it / 1000.0) }
                if (plan[3] in SOTA_MODES) mode = plan[3]
            }
        }
    }

    // ------------------------------------------------ search & cascade
    fun onQuery(q: String) {
        query = q
        searchJob?.cancel()
        if (q.isBlank()) {
            suggestions = emptyList(); return
        }
        searchJob = viewModelScope.launch(Dispatchers.IO) {
            delay(120) // debounce: one query per pause in typing
            val r = db.search(q, preferAssoc = preferredAssoc())
            withContext(Dispatchers.Main) { if (query == q) suggestions = r }
        }
    }

    private fun preferredAssoc(): String = assoc?.code ?: summit?.assoc ?: recents.firstOrNull()?.assoc ?: "F"

    fun chooseAssoc(a: Named) {
        assoc = a
        region = null
        regionSummits = emptyList()
        viewModelScope.launch(Dispatchers.IO) {
            val r = db.regions(a.code)
            withContext(Dispatchers.Main) { regions = r }
        }
    }

    fun chooseRegion(r: Named) {
        region = r
        val a = assoc ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val s = db.summitsIn(a.code, r.code)
            withContext(Dispatchers.Main) { regionSummits = s }
        }
    }

    fun pick(s: Summit, remember: Boolean = true) {
        summit = s
        query = ""
        suggestions = emptyList()
        nearby = null
        // Keep the cascade in step with the choice.
        val a = assocs.firstOrNull { it.code == s.assoc } ?: Named(s.assoc, s.assoc)
        if (assoc?.code != a.code) chooseAssoc(a)
        region = Named(s.region, s.regionName)
        viewModelScope.launch(Dispatchers.IO) {
            val rs = db.regions(s.assoc)
            val list = db.summitsIn(s.assoc, s.region)
            withContext(Dispatchers.Main) { regions = rs; regionSummits = list }
        }
        prefs.edit().putString("sota.summit", s.code).apply()
        if (remember) {
            val codes = (listOf(s.code) + recents.map { it.code }).distinct().take(8)
            prefs.edit().putString("sota.recents", codes.joinToString(",")).apply()
            recents = (listOf(s) + recents.filter { it.code != s.code }).take(8)
        }
    }

    /** Nearest summits from the T-Echo's own GPS fix. */
    fun findNearby(onError: (String) -> Unit) {
        viewModelScope.launch {
            val st = runCatching { techo.node.refreshStatus() }.getOrElse { techo.node.status.value }
            val gps = st?.optJSONObject("gps")
            if (gps == null || !gps.optBoolean("fix")) {
                onError(L.s(R.string.sota_no_gps)); return@launch
            }
            val lat = gps.optDouble("lat"); val lon = gps.optDouble("lon")
            val r = withContext(Dispatchers.IO) { db.nearby(lat, lon) }
            nearby = r
            if (r.isEmpty()) onError(L.s(R.string.sota_none_near))
        }
    }

    fun refreshList(onDone: (String) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            loading = L.s(R.string.sota_downloading)
            val r = runCatching { db.downloadAndImport { n -> loading = L.s(R.string.sota_importing, n) } }
            loadLists()
            loading = null
            withContext(Dispatchers.Main) {
                onDone(r.fold({ L.s(R.string.sota_list_updated) }, { L.s(R.string.sota_download_failed, it.message ?: "") }))
            }
        }
    }

    // ------------------------------------------------ spot
    val callsign: String get() = if (prefix.isBlank()) baseCall.trim() else "${prefix.trim()}/${baseCall.trim()}"

    /** "14.062" from "14,062", "14062" (kHz) or "14.0620"; always 3 decimals, as the T-Echo does. */
    fun normalizedFreq(): String? {
        val t = freq.replace(',', '.').trim()
        val v = t.toDoubleOrNull() ?: return null
        val mhz = if (!t.contains('.') && v >= 1000) v / 1000.0 else v // "14062" = kHz
        if (mhz <= 0 || mhz > 250_000) return null
        return String.format(Locale.US, "%.3f", mhz)
    }

    fun message(): String? {
        val s = summit ?: return null
        val f = normalizedFreq() ?: return null
        if (baseCall.isBlank()) return null
        return Aprs.sotaSpot(s.code, f, mode, callsign.uppercase(), comment)
    }

    fun sendSpot(onResult: (String) -> Unit) {
        val text = message() ?: return
        if (text.length > 67) {
            onResult(L.s(R.string.sota_too_long, text.length)); return
        }
        busy = true
        viewModelScope.launch {
            val before = System.currentTimeMillis()
            val r = techo.sendMessage(Aprs.SOTA_GATEWAY, text)
            busy = false
            val row = techo.messages.messages(Aprs.SOTA_GATEWAY).lastOrNull { it.outgoing }
            track = row?.let { SpotTrack(it.rowId, before, it.msgId) }
            onResult(r.fold({ L.s(R.string.sota_spot_sent, text) }, { L.s(R.string.failed, it.message ?: "") }))
            // The T-Echo's SOTA screen shows the saved plan: keep it in step with the spot.
            if (r.isSuccess && techo.link.hasConfigChannel) savePlanOnNode { ok, m -> if (!ok) onResult(m) }
        }
    }

    /** State of the last spot: the node's ack and the gateway's answer, if any. */
    fun spotStatus(): Pair<MsgState, String?>? {
        val t = track ?: return null
        val msgs = techo.messages.messages(Aprs.SOTA_GATEWAY)
        val mine = msgs.firstOrNull { it.rowId == t.rowId } ?: return null
        val answer = msgs.lastOrNull { !it.outgoing && it.time >= t.sentAt }?.text
        return mine.state to answer
    }

    /** Stores the plan on the T-Echo, so its own screen shows it and its menu can re-spot. */
    fun savePlanOnNode(onResult: (ok: Boolean, msg: String) -> Unit) {
        val s = summit ?: return
        val f = normalizedFreq()?.toDoubleOrNull() ?: return onResult(false, L.s(R.string.sota_bad_freq))
        if (f >= 1000) return onResult(false, L.s(R.string.sota_freq_limit))
        val digits = String.format(Locale.US, "%07.3f", f).replace(".", "")
        val pfx = prefix.trim().uppercase().takeIf { p -> assocs.any { it.code == p } } ?: ""
        val recentsCsv = (listOf(s.assoc) + (techo.node.config.value?.optString("sotaRecent") ?: "").split(','))
            .filter { it.isNotBlank() }.distinct().take(4).joinToString(",")
        val changes = JSONObject()
            .put("sotaPlan", "${s.code},$pfx,$digits,$mode")
            .put("sotaLast", "${s.code},$pfx")
            .put("sotaRecent", recentsCsv)
        viewModelScope.launch {
            val r = runCatching { techo.node.setConfig(changes) }
            onResult(r.isSuccess, r.fold({
                if (prefix.isNotBlank() && pfx.isEmpty()) L.s(R.string.sota_plan_saved_pfx, prefix)
                else L.s(R.string.sota_plan_saved)
            }, { L.s(R.string.failed, it.message ?: "") }))
        }
    }

    fun applyPreset(f: String, m: String) {
        freq = f
        mode = m
    }

}
