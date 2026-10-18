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

/** Everything the screens share: the BLE link, the node protocol and the local databases. */
class Techo(val ctx: Context) {
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    val prefs: SharedPreferences = ctx.getSharedPreferences("techo", Context.MODE_PRIVATE)
    val link = TechoLink(ctx)
    val node = NodeClient(link, scope)
    val messages = MessageDb(ctx)
    val summits = SummitDb(ctx)

    private val _heard = MutableStateFlow<List<String>>(emptyList())
    /** Last raw frames heard by the node (TNC2 text), newest last. */
    val heard: StateFlow<List<String>> = _heard.asStateFlow()

    /** Conversation currently on screen (no notification for it). */
    @Volatile var openConversation: String? = null

    /** The node's callsign: from its config, or the last one we saw. */
    val myCall: String
        get() = node.config.value?.optString("callsign")?.takeIf { it.isNotBlank() }
            ?: prefs.getString("callsign", null) ?: "NOCALL"

    init {
        createChannels()
        scope.launch { link.kissFrames.collect { onFrame(it) } }
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
        // Messages the node gave up on: one send + msgRetries resends, 30 s apart.
        scope.launch {
            while (true) {
                val retries = node.config.value?.optInt("msgRetries", 3) ?: 3
                messages.expireSent((retries + 1) * 30_000L + 20_000L)
                delay(10_000)
            }
        }
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
    private fun onFrame(ax25: ByteArray) {
        val f = Ax25.decode(ax25) ?: return
        _heard.value = (_heard.value + f.toString()).takeLast(100)
        val m = Aprs.parseMessage(f) ?: return
        if (!Aprs.sameCall(m.to, myCall)) return
        when (m) {
            is AprsMessage.Ack -> messages.ack(m.from, m.id, m.rejected)
            is AprsMessage.Text -> {
                // The node acks it on the air by itself (aprs.cpp handleRemoteMessage).
                if (messages.addIncoming(m.from, m.text, m.id) &&
                    !Aprs.sameCall(openConversation ?: "", m.from)
                ) notifyMessage(m.from, m.text)
            }
        }
    }

    // ---------------------------------------------------------------- TX
    /**
     * Sends an APRS message. With the configuration channel the node numbers it, repeats it until
     * acked and shows it on its screen; with an older firmware (KISS only) the app builds the
     * frame itself and sends it once.
     */
    suspend fun sendMessage(to: String, text: String): Result<Unit> {
        val peer = to.trim().uppercase()
        val row = messages.addOutgoing(peer, text)
        return runCatching {
            if (link.hasConfigChannel) {
                val r = node.sendMessage(peer, text)
                messages.update(row, MsgState.SENT, r.id)
            } else if (link.isReady) {
                // Without the configuration channel the frame carries the callsign the app knows:
                // never send one as NOCALL (nobody would ack it, and it should not be on the air).
                if (myCall.startsWith("NOCALL")) error(L.s(R.string.err_nocall))
                val id = "%03d".format((prefs.getInt("msgSeq", 0) + 1) % 1000)
                prefs.edit().putInt("msgSeq", id.toInt()).apply()
                val frame = TextFrame(myCall, "APL2OY", listOf("WIDE1-1"), ":%-9s:%s{%s".format(peer, text, id))
                val ax = Ax25.encode(frame) ?: error(L.s(R.string.err_bad_call, myCall))
                link.sendKiss(Kiss.encode(ax))
                messages.update(row, MsgState.SENT, id)
            } else error(L.s(R.string.err_not_connected))
        }.onFailure { messages.update(row, MsgState.FAILED, note = it.message) }
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
            .build()
        runCatching { NotificationManagerCompat.from(ctx).notify(from.hashCode(), n) }
    }

    companion object {
        const val CH_MSG = "messages"
        const val CH_LINK = "link"
    }
}
