package fr.f4lek.techoaprs

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
        ServiceCompat.startForeground(
            this, NOTIF_ID, build(L.s(R.string.svc_connecting)),
            if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE else 0
        )
        watch?.cancel()
        watch = techo.scope.launch {
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
        .setContentIntent(
            PendingIntent.getActivity(
                this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
            )
        )
        .build()

    companion object {
        private const val NOTIF_ID = 1

        fun start(ctx: Context) {
            runCatching { ctx.startForegroundService(Intent(ctx, LinkService::class.java)) }
        }

        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, LinkService::class.java))
        }
    }
}
