package fr.f4lek.techoaprs

import fr.f4lek.techoaprs.net.IsState
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import fr.f4lek.techoaprs.ble.LinkState
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/** Keeps the BLE link (and incoming messages) alive while the app is in the background. */
class LinkService : Service() {
    private var watch: Job? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val techo = (application as TechoApp).techo
        // "Stop" button of the notification: drop the link (Bluetooth or APRS-IS) and the
        // service, until the app is opened again.
        if (intent?.action == ACTION_STOP) {
            watch?.cancel()
            techo.stopLinks()
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }
        // Web mode keeps an APRS-IS connection for messages ("remoteMessaging"); T-Echo mode keeps
        // the Bluetooth link ("connectedDevice"). Android 14+ wants the type that is in use.
        val type = when {
            Build.VERSION.SDK_INT >= 34 && techo.isWeb -> ServiceInfo.FOREGROUND_SERVICE_TYPE_REMOTE_MESSAGING
            Build.VERSION.SDK_INT >= 29 && !techo.isWeb -> ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            else -> 0
        }
        ServiceCompat.startForeground(this, NOTIF_ID, build(L.s(R.string.svc_connecting)), type)
        watch?.cancel()
        watch = if (techo.isWeb) techo.scope.launch {
            techo.aprsIs.state.collect { st ->
                getSystemService(android.app.NotificationManager::class.java).notify(NOTIF_ID, build(webStateText(st)))
            }
        } else techo.scope.launch {
            techo.link.state.collect { st ->
                val text = when (st) {
                    is LinkState.Ready -> L.s(R.string.svc_connected, st.name)
                    is LinkState.Connecting -> st.step
                    is LinkState.Failed -> st.reason
                    else -> L.s(R.string.link_disconnected)
                }
                getSystemService(android.app.NotificationManager::class.java).notify(NOTIF_ID, build(text))
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        watch?.cancel()
        super.onDestroy()
    }

    private fun build(text: String) = NotificationCompat.Builder(this, Techo.CH_LINK)
        .setSmallIcon(R.drawable.ic_stat_techo)
        .setContentTitle("T-Echo APRS")
        .setContentText(text)
        .setOngoing(true)
        // Its own group: otherwise Android folds it in with the message notifications and its
        // "Stop" button is two taps away.
        .setGroup("fr.f4lek.techoaprs.link")
        .setContentIntent(
            PendingIntent.getActivity(
                this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
            )
        )
        .addAction(
            0, L.s(R.string.notif_stop),
            PendingIntent.getService(
                this, 1, Intent(this, LinkService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE
            )
        )
        .build()

    companion object {
        private const val ACTION_STOP = "fr.f4lek.techoaprs.STOP"

        /** One line about the APRS-IS connection, for the notification and the settings. */
        fun webStateText(st: IsState): String = when (st) {
            is IsState.Connected -> if (st.verified) L.s(R.string.web_state_on, st.server)
                else L.s(R.string.web_state_unverified, st.server)
            is IsState.Connecting -> L.s(R.string.web_state_connecting, st.server)
            is IsState.Failed -> L.s(R.string.web_state_failed, st.reason)
            IsState.Off -> L.s(R.string.web_state_off)
        }

        private const val NOTIF_ID = 1

        fun start(ctx: Context) {
            runCatching { ctx.startForegroundService(Intent(ctx, LinkService::class.java)) }
        }

        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, LinkService::class.java))
        }
    }
}
