package fr.f4lek.techoaprs.net

import kotlin.coroutines.coroutineContext
import fr.f4lek.techoaprs.proto.TextFrame
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException

sealed class IsState {
    data object Off : IsState()
    data class Connecting(val server: String) : IsState()
    /** Logged in. [verified] = the passcode was accepted, so the server takes our frames. */
    data class Connected(val server: String, val verified: Boolean) : IsState()
    data class Failed(val reason: String) : IsState()
}

/**
 * APRS-IS client (the Internet side of APRS): one TCP connection to a server of the network,
 * login with the callsign and its passcode, a filter on the messages addressed to that callsign.
 * Used in "Web" mode, when there is no T-Echo: the app is then a station of its own.
 */
class AprsIsLink(private val scope: CoroutineScope) {
    private val _state = MutableStateFlow<IsState>(IsState.Off)
    val state: StateFlow<IsState> = _state.asStateFlow()

    private val _frames = MutableSharedFlow<TextFrame>(extraBufferCapacity = 64)
    /** Frames the server sends us (messages for our callsign and their acks). */
    val frames: SharedFlow<TextFrame> = _frames.asSharedFlow()

    private var job: Job? = null
    @Volatile private var out: OutputStream? = null
    @Volatile private var socket: Socket? = null

    val isConnected: Boolean get() = (_state.value as? IsState.Connected)?.verified == true

    /** (Re)starts the connection loop with these credentials. */
    fun start(call: String, passcode: Int, server: String) {
        stop()
        val (host, port) = splitServer(server)
        job = scope.launch(Dispatchers.IO) {
            var backoff = 5_000L
            while (isActive) {
                _state.value = IsState.Connecting(host)
                val r = runCatching { session(call, passcode, host, port) }
                closeSocket()
                if (!isActive) break
                // A session that got logged in was fine: retry quickly after it drops.
                if (_state.value is IsState.Connected) backoff = 5_000L
                _state.value = IsState.Failed(r.exceptionOrNull()?.message ?: "connection closed")
                delay(backoff)
                backoff = (backoff * 2).coerceAtMost(60_000L)
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
        closeSocket()
        _state.value = IsState.Off
    }

    /** Sends one TNC2 line ("CALL>DEST,PATH:info"). False when not logged in with a passcode. */
    fun send(line: String): Boolean {
        if (!isConnected) return false
        val o = out ?: return false
        return runCatching {
            synchronized(this) { o.write((line + "\r\n").toByteArray(Charsets.ISO_8859_1)); o.flush() }
        }.isSuccess
    }

    private suspend fun session(call: String, passcode: Int, host: String, port: Int) {
        val s = Socket()
        socket = s
        s.connect(InetSocketAddress(host, port), 15_000)
        // The server sends a "#" keepalive about every 20 s: 70 s of silence = dead link.
        s.soTimeout = 70_000
        s.keepAlive = true
        val reader = BufferedReader(InputStreamReader(s.getInputStream(), Charsets.ISO_8859_1))
        out = s.getOutputStream()
        val login = "user $call pass $passcode vers $SOFTWARE $VERSION filter g/$call"
        synchronized(this) { out!!.write((login + "\r\n").toByteArray(Charsets.ISO_8859_1)); out!!.flush() }
        while (coroutineContext.isActive) {
            val line = try {
                reader.readLine()
            } catch (e: SocketTimeoutException) {
                throw IllegalStateException("no data from the server")
            } ?: throw IllegalStateException("connection closed by the server")
            if (line.startsWith("#")) {
                // "# logresp F4LEK-6 verified, server T2FRANCE"
                if (line.startsWith("# logresp")) {
                    val verified = Regex("logresp \\S+ verified").containsMatchIn(line)
                    val server = Regex("server (\\S+)").find(line)?.groupValues?.get(1) ?: host
                    _state.value = IsState.Connected(server, verified)
                }
                continue
            }
            TextFrame.parse(line.trimEnd('\r', '\n'))?.let { _frames.tryEmit(it) }
        }
    }

    private fun closeSocket() {
        out = null
        socket?.let { runCatching { it.close() } }
        socket = null
    }

    companion object {
        /** Software identifier ("tocall") of the app on APRS-IS: the destination of its frames. */
        const val TOCALL = "APZLEK"
        const val SOFTWARE = "TEchoAPRS-F4LEK"
        const val VERSION = "0.2"
        const val DEFAULT_SERVER = "rotate.aprs2.net:14580"

        /**
         * The APRS-IS passcode of a callsign (public algorithm, the same in every APRS client):
         * a 15-bit hash of the callsign without its SSID.
         */
        fun passcode(callsign: String): Int {
            val call = callsign.trim().uppercase().substringBefore('-')
            var hash = 0x73e2
            var i = 0
            while (i < call.length) {
                hash = hash xor (call[i].code shl 8)
                if (i + 1 < call.length) hash = hash xor call[i + 1].code
                i += 2
            }
            return hash and 0x7fff
        }

        /** "host:port", port 14580 (the filtered port) when missing. */
        fun splitServer(server: String): Pair<String, Int> {
            val s = server.trim().ifEmpty { DEFAULT_SERVER }
            val i = s.lastIndexOf(':')
            return if (i > 0) s.substring(0, i) to (s.substring(i + 1).toIntOrNull() ?: 14580) else s to 14580
        }

        /** A callsign we may log in with: real (not N0CALL/NOCALL), SSID 0-15 or none. */
        fun validCall(c: String): Boolean {
            val u = c.trim().uppercase()
            if (u.startsWith("N0CALL") || u.startsWith("NOCALL")) return false
            return Regex("^[A-Z0-9]{1,2}[0-9][A-Z0-9]{0,4}(-([0-9]|1[0-5]))?$").matches(u)
        }
    }
}
