package fr.f4lek.techoaprs.node

import fr.f4lek.techoaprs.R
import fr.f4lek.techoaprs.L
import fr.f4lek.techoaprs.ble.TechoLink
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
import org.json.JSONObject

class NodeException(msg: String) : Exception(msg)

/**
 * The node's line protocol (docs/protocol_config_v1.md) over the BLE configuration channel.
 *
 * The firmware does not tag replies with a request id, so commands go one at a time ([mutex])
 * and the reply is recognised by its shape (`config`, `status`, `msg`, `beacon`, or `ok:false`).
 * Lines that match nothing (auto-beacon notes, digi log, CLI text) are only logged.
 */
class NodeClient(private val link: TechoLink, scope: CoroutineScope) {
    private val mutex = Mutex()
    @Volatile
    private var waiter: Pair<(JSONObject) -> Boolean, CompletableDeferred<JSONObject>>? = null

    private val _config = MutableStateFlow<JSONObject?>(null)
    /** Last config read back from the node (null until the first `get`). */
    val config: StateFlow<JSONObject?> = _config.asStateFlow()

    private val _status = MutableStateFlow<JSONObject?>(null)
    val status: StateFlow<JSONObject?> = _status.asStateFlow()

    private val _log = MutableStateFlow<List<String>>(emptyList())
    val log: StateFlow<List<String>> = _log.asStateFlow()

    init {
        scope.launch { link.cfgLines.collect { onLine(it) } }
    }

    private fun onLine(line: String) {
        _log.value = (_log.value + line).takeLast(200)
        if (!line.startsWith("{")) return
        val obj = runCatching { JSONObject(line) }.getOrNull() ?: return
        obj.optJSONObject("config")?.let { _config.value = it }
        obj.optJSONObject("status")?.let { _status.value = it }
        val w = waiter ?: return
        if (w.first(obj)) {
            waiter = null
            w.second.complete(obj)
        }
    }

    private suspend fun request(cmd: JSONObject, timeoutMs: Long = 6000, match: (JSONObject) -> Boolean): JSONObject =
        mutex.withLock {
            if (!link.hasConfigChannel) throw NodeException(L.s(R.string.node_no_cfg))
            val d = CompletableDeferred<JSONObject>()
            waiter = { o: JSONObject -> match(o) || (o.has("ok") && !o.optBoolean("ok") && o.has("error")) } to d
            link.sendLine(cmd.toString())
            val r = withTimeoutOrNull(timeoutMs) { d.await() }
            waiter = null
            r ?: throw NodeException(L.s(R.string.node_no_answer))
            if (!r.optBoolean("ok", true) && r.has("error")) throw NodeException(r.optString("error"))
            r
        }

    suspend fun refreshConfig(): JSONObject =
        request(JSONObject().put("cmd", "get")) { it.has("config") }.getJSONObject("config")

    suspend fun refreshStatus(): JSONObject =
        request(JSONObject().put("cmd", "status")) { it.has("status") }.getJSONObject("status")

    /**
     * Applies a partial config. The node reads at most 1024 bytes per BLE line, so big changes
     * are split in several `set` commands (each one is validated and saved atomically).
     */
    suspend fun setConfig(changes: JSONObject): JSONObject {
        val keys = changes.keys().asSequence().toList()
        var batch = JSONObject()
        var last: JSONObject? = null
        for (k in keys) {
            val trial = JSONObject(batch.toString()).put(k, changes.get(k))
            if (batch.length() > 0 && wrapSet(trial).length > 900) {
                last = sendSet(batch)
                batch = JSONObject()
            }
            batch.put(k, changes.get(k))
        }
        if (batch.length() > 0) last = sendSet(batch)
        return last ?: refreshConfig()
    }

    private fun wrapSet(cfg: JSONObject) = JSONObject().put("cmd", "set").put("config", cfg).toString()

    private suspend fun sendSet(cfg: JSONObject): JSONObject =
        request(JSONObject().put("cmd", "set").put("config", cfg), 8000) { it.has("config") }.getJSONObject("config")

    data class MsgResult(val to: String, val id: String?)

    /** APRS message through the node: it numbers it, resends it every 30 s and waits for the ack. */
    suspend fun sendMessage(to: String, text: String): MsgResult {
        val r = request(JSONObject().put("cmd", "msg").put("to", to).put("text", text)) { it.has("msg") }
        val m = r.getJSONObject("msg")
        if (!m.optBoolean("tx")) throw NodeException(L.s(R.string.node_msg_not_sent, m.optInt("code")))
        return MsgResult(m.optString("to", to), if (m.has("id")) "%03d".format(m.getInt("id")) else null)
    }

    suspend fun beacon(): String {
        val b = request(JSONObject().put("cmd", "beacon"), 15_000) { it.has("beacon") }.getJSONObject("beacon")
        return if (b.optBoolean("tx")) L.s(R.string.beacon_sent)
        else L.s(R.string.beacon_not_sent, b.optString("why", L.s(R.string.beacon_code, b.optInt("code"))))
    }

    suspend fun reboot() {
        mutex.withLock { link.sendLine(JSONObject().put("cmd", "reboot").toString()) }
    }

    suspend fun factoryReset(): JSONObject =
        request(JSONObject().put("cmd", "factory_reset")) { it.has("config") }.getJSONObject("config")

    /** Raw CLI line (token mode), for the console. The answer shows up in [log]. */
    suspend fun cli(line: String) {
        mutex.withLock { link.sendLine(line) }
    }

    fun forget() {
        _config.value = null
        _status.value = null
    }

    companion object {
        /** JSON value comparison that treats 1 and 1.0 as equal (floats come back clamped). */
        fun same(a: Any?, b: Any?): Boolean {
            if (a is Number && b is Number) return Math.abs(a.toDouble() - b.toDouble()) < 1e-6
            if (a is JSONArray && b is JSONArray) return a.toString() == b.toString()
            return a == b
        }
    }
}
