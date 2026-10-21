package fr.f4lek.techoaprs

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import fr.f4lek.techoaprs.ble.LinkState
import fr.f4lek.techoaprs.ble.TechoLink
import fr.f4lek.techoaprs.data.MessageDb
import fr.f4lek.techoaprs.data.MsgState
import fr.f4lek.techoaprs.data.SummitDb
import fr.f4lek.techoaprs.net.AprsIsLink
import fr.f4lek.techoaprs.node.NodeClient
import fr.f4lek.techoaprs.proto.Aprs
import fr.f4lek.techoaprs.proto.AprsMessage
import fr.f4lek.techoaprs.proto.Ax25
import fr.f4lek.techoaprs.proto.Kiss
import fr.f4lek.techoaprs.proto.TextFrame
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.util.concurrent.ConcurrentHashMap

/** Where the app sends and receives: the T-Echo over Bluetooth, or APRS-IS over the Internet. */
enum class Source(val key: String) {
    TECHO("techo"), WEB("web");

    companion object {
        fun of(k: String?) = entries.firstOrNull { it.key == k } ?: TECHO
    }
}

/** Everything the screens share: the links (Bluetooth, APRS-IS), the node protocol, the databases. */
class Techo(val ctx: Context) {
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    val prefs: SharedPreferences = ctx.getSharedPreferences("techo", Context.MODE_PRIVATE)
    val link = TechoLink(ctx)
    val node = NodeClient(link, scope)
    val aprsIs = AprsIsLink(scope)
    val messages = MessageDb(ctx)
    val summits = SummitDb(ctx)

    private val _source = MutableStateFlow(Source.of(prefs.getString("source", null)))
    val source: StateFlow<Source> = _source.asStateFlow()
    val isWeb: Boolean get() = _source.value == Source.WEB

    private val _heard = MutableStateFlow<List<String>>(emptyList())
    /** Last raw frames heard by the node (TNC2 text), newest last. */
    val heard: StateFlow<List<String>> = _heard.asStateFlow()

    /** Conversation currently on screen (no notification for it). */
    @Volatile var openConversation: String? = null

    // ---------------------------------------------------------------- Web identity
    val webCall: String get() = prefs.getString("web.call", DEFAULT_WEB_CALL) ?: DEFAULT_WEB_CALL
    /** Passcode typed by the user, or null: computed from the callsign. */
    val webPassOverride: Int? get() = prefs.getString("web.pass", "")?.trim()?.toIntOrNull()
    val webServer: String get() = prefs.getString("web.server", AprsIsLink.DEFAULT_SERVER) ?: AprsIsLink.DEFAULT_SERVER

    /** Our callsign: the Web identity, or the node's (from its config, or the last one seen). */
    val myCall: String
        get() = if (isWeb) webCall else node.config.value?.optString("callsign")?.takeIf { it.isNotBlank() }
            ?: prefs.getString("callsign", null) ?: "NOCALL"

    init {
        createChannels()
        scope.launch { link.kissFrames.collect { ax -> Ax25.decode(ax)?.let { onFrame(it, viaWeb = false) } } }
        scope.launch { aprsIs.frames.collect { onFrame(it, viaWeb = true) } }
        scope.launch {
            link.state.collect { st ->
                if (st is LinkState.Ready) {
                    prefs.edit().putString("lastDevice", prefs.getString("connecting", null)).apply()
                    if (st.hasConfig) runCatching {
                        delay(300)
                        val c = node.refreshConfig()
                        prefs.edit().putString("callsign", c.optString("callsign")).apply()
                        node.refreshStatus()
                    }
                } else if (st is LinkState.Idle || st is LinkState.Failed) {
                    node.forget()
                }
            }
        }
        // Messages nobody acked: the node (or the Web outbox) sends once, resends after 60 s,
        // then every 120 s, and gives up 120 s after the last resend.
        scope.launch {
            while (true) {
                val retries = if (isWeb) WEB_RETRIES else node.config.value?.optInt("msgRetries", 3) ?: 3
                messages.expireSent(60_000L + retries * 120_000L + 20_000L)
                delay(10_000)
            }
        }
        scope.launch { webOutboxLoop() }
        if (isWeb) startWeb()
    }

    // ---------------------------------------------------------------- source
    /** Switches T-Echo <-> Web. The activity is recreated by the caller (tabs, permissions). */
    fun setSource(s: Source) {
        if (s == _source.value) return
        prefs.edit().putString("source", s.key).apply()
        _source.value = s
        if (s == Source.WEB) {
            link.disconnect()
            startWeb()
        } else {
            aprsIs.stop()
            outbox.clear()
            LinkService.stop(ctx)
        }
    }

    /** Saves the Web identity and (re)connects with it. */
    fun setWebIdentity(call: String, pass: String, server: String) {
        prefs.edit()
            .putString("web.call", call.trim().uppercase())
            .putString("web.pass", pass.trim())
            .putString("web.server", server.trim().ifEmpty { AprsIsLink.DEFAULT_SERVER })
            .apply()
        if (isWeb) startWeb()
    }

    /** Connects to APRS-IS with the Web identity, unless it is still the N0CALL placeholder. */
    fun startWeb() {
        if (!AprsIsLink.validCall(webCall)) {
            aprsIs.stop()
            return
        }
        aprsIs.start(webCall.uppercase(), webPassOverride ?: AprsIsLink.passcode(webCall), webServer)
        LinkService.start(ctx)
    }

    /** Notification "Stop": no more link (nor reconnection attempts) until the app is reopened. */
    fun stopLinks() {
        if (isWeb) aprsIs.stop() else link.disconnect()
    }

    fun connect(address: String) {
        prefs.edit().putString("connecting", address).apply()
        LinkService.start(ctx)
        link.connect(address)
    }

    fun disconnect() {
        link.disconnect()
        LinkService.stop(ctx)
    }

    // ---------------------------------------------------------------- RX
    private fun onFrame(f: TextFrame, viaWeb: Boolean) {
        if (viaWeb != isWeb) return // only the active source counts
        _heard.value = (_heard.value + f.toString()).takeLast(100)
        val m = Aprs.parseMessage(f) ?: return
        if (!Aprs.sameCall(m.to, myCall)) return
        when (m) {
            is AprsMessage.Ack -> {
                messages.ack(m.from, m.id, m.rejected)
                outbox.values.removeAll { Aprs.sameCall(it.peer, m.from) && sameId(it.id, m.id) }
            }
            is AprsMessage.Text -> {
                // On the T-Echo the node acks on the air by itself (aprs.cpp handleRemoteMessage).
                // On the Web the app is the station: ack EVERY copy, as a resend means our ack
                // got lost.
                if (viaWeb && m.id != null) sendWebLine(":%-9s:ack%s".format(m.from.uppercase(), m.id))
                if (messages.addIncoming(m.from, m.text, m.id) &&
                    !Aprs.sameCall(openConversation ?: "", m.from)
                ) notifyMessage(m.from, m.text)
            }
        }
    }

    /** Demo build only (screenshots): a frame as if the node's radio had heard it. */
    internal fun demoFrame(tnc2: String) {
        TextFrame.parse(tnc2)?.let { onFrame(it, viaWeb = isWeb) }
    }

    // ---------------------------------------------------------------- TX
    /**
     * Sends an APRS message through the active source.
     * - T-Echo with the configuration channel: the node numbers it, repeats it until acked and
     *   shows it on its screen. With an older firmware (KISS only) the app sends the frame once.
     * - Web: the app numbers it and repeats it itself (see [webOutboxLoop]).
     */
    suspend fun sendMessage(to: String, text: String): Result<Unit> {
        val peer = to.trim().uppercase()
        val row = messages.addOutgoing(peer, text)
        return runCatching {
            if (isWeb) {
                if (!aprsIs.isConnected) error(L.s(R.string.err_web_not_connected))
                val id = nextMsgId("webMsgSeq")
                val p = Pending(row, peer, text, id)
                if (!sendWebLine(p.info())) error(L.s(R.string.err_web_not_connected))
                outbox[row] = p
                messages.update(row, MsgState.SENT, id)
            } else if (link.hasConfigChannel) {
                val r = node.sendMessage(peer, text)
                messages.update(row, MsgState.SENT, r.id)
            } else if (link.isReady) {
                // Without the configuration channel the frame carries the callsign the app knows:
                // never send one as NOCALL (nobody would ack it, and it should not be on the air).
                if (myCall.startsWith("NOCALL")) error(L.s(R.string.err_nocall))
                val id = nextMsgId("msgSeq")
                val frame = TextFrame(myCall, "APL2OY", listOf("WIDE1-1"), ":%-9s:%s{%s".format(peer, text, id))
                val ax = Ax25.encode(frame) ?: error(L.s(R.string.err_bad_call, myCall))
                link.sendKiss(Kiss.encode(ax))
                messages.update(row, MsgState.SENT, id)
            } else error(L.s(R.string.err_not_connected))
        }.onFailure { messages.update(row, MsgState.FAILED, note = it.message) }
    }

    private fun nextMsgId(key: String): String {
        val n = prefs.getInt(key, 0) % 999 + 1
        prefs.edit().putInt(key, n).apply()
        return "%03d".format(n)
    }

    private fun sameId(a: String, b: String) = a.trimStart('0') == b.trimStart('0')

    /** "CALL>APZLEK,TCPIP*:info" on APRS-IS. */
    private fun sendWebLine(info: String): Boolean =
        aprsIs.send(TextFrame(webCall.uppercase(), AprsIsLink.TOCALL, listOf("TCPIP*"), info).toString())

    /** A Web message waiting for its ack. */
    private class Pending(val row: Long, val peer: String, val text: String, val id: String) {
        var sends = 1
        var nextAt = System.currentTimeMillis() + 60_000L
        fun info() = ":%-9s:%s{%s".format(peer, text, id)
    }

    private val outbox = ConcurrentHashMap<Long, Pending>()

    /** Same rhythm as the firmware (aprs.cpp): resend after 60 s, then every 120 s, then give up. */
    private suspend fun webOutboxLoop() {
        while (true) {
            delay(1_000)
            val now = System.currentTimeMillis()
            for (p in outbox.values) {
                if (now < p.nextAt) continue
                if (p.sends > WEB_RETRIES) {
                    outbox.remove(p.row) // expireSent() marks it "no ack"
                    continue
                }
                if (sendWebLine(p.info())) p.sends++
                p.nextAt = now + 120_000L
            }
        }
    }

    // ---------------------------------------------------------------- notifications
    fun createChannels() {
        val nm = ctx.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CH_MSG, L.s(R.string.ch_messages), NotificationManager.IMPORTANCE_HIGH)
        )
        nm.createNotificationChannel(
            NotificationChannel(CH_LINK, L.s(R.string.ch_link), NotificationManager.IMPORTANCE_LOW)
        )
    }

    private fun notifyMessage(from: String, text: String) {
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(ctx, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return
        val open = PendingIntent.getActivity(
            ctx, from.hashCode(),
            Intent(ctx, MainActivity::class.java).putExtra(MainActivity.EXTRA_PEER, from)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val n = NotificationCompat.Builder(ctx, CH_MSG)
            .setSmallIcon(R.drawable.ic_stat_techo)
            .setContentTitle(from)
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setContentIntent(open)
            .setAutoCancel(true)
            .setGroup("fr.f4lek.techoaprs.messages")
            .build()
        runCatching { NotificationManagerCompat.from(ctx).notify(from.hashCode(), n) }
    }

    companion object {
        const val CH_MSG = "messages"
        const val CH_LINK = "link"
        const val DEFAULT_WEB_CALL = "N0CALL-2"
        const val WEB_RETRIES = 3
    }
}
