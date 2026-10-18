package fr.f4lek.techoaprs

import android.Manifest
import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import kotlinx.coroutines.flow.MutableStateFlow
import fr.f4lek.techoaprs.ui.AppRoot
import fr.f4lek.techoaprs.ui.TechoTheme

class MainActivity : ComponentActivity() {
    /** Conversation to open (tap on a message notification). */
    val openPeer = MutableStateFlow<String?>(null)

    private val askPerms = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        if (granted.values.all { it }) autoReconnect()
    }

    override fun attachBaseContext(newBase: android.content.Context) {
        super.attachBaseContext(L.wrap(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        val techo = (application as TechoApp).techo
        openPeer.value = intent.getStringExtra(EXTRA_PEER)
        setContent {
            TechoTheme { AppRoot(techo, openPeer) }
        }
        askPerms.launch(neededPermissions())
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        intent.getStringExtra(EXTRA_PEER)?.let { openPeer.value = it }
    }

    private fun autoReconnect() {
        val techo = (application as TechoApp).techo
        val last = techo.prefs.getString("lastDevice", null) ?: return
        if (techo.link.state.value is fr.f4lek.techoaprs.ble.LinkState.Idle && techo.link.bluetoothOn) techo.connect(last)
    }

    companion object {
        const val EXTRA_PEER = "peer"

        fun neededPermissions(): Array<String> = buildList {
            if (Build.VERSION.SDK_INT >= 31) {
                add(Manifest.permission.BLUETOOTH_SCAN)
                add(Manifest.permission.BLUETOOTH_CONNECT)
            } else {
                add(Manifest.permission.ACCESS_COARSE_LOCATION)
                add(Manifest.permission.ACCESS_FINE_LOCATION)
            }
            if (Build.VERSION.SDK_INT >= 33) add(Manifest.permission.POST_NOTIFICATIONS)
        }.toTypedArray()
    }
}
