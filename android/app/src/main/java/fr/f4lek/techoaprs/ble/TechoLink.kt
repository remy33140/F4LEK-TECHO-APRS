package fr.f4lek.techoaprs.ble

import fr.f4lek.techoaprs.R
import fr.f4lek.techoaprs.L
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.UUID

object TechoUuids {
    // KISS TNC service, as advertised by the firmware (ble_kiss.cpp) and APRSdroid.
    val KISS_SVC: UUID = UUID.fromString("00000001-ba2a-46c9-ae49-01b0961f68bb")
    val KISS_RX: UUID = UUID.fromString("00000002-ba2a-46c9-ae49-01b0961f68bb") // phone -> node
    val KISS_TX: UUID = UUID.fromString("00000003-ba2a-46c9-ae49-01b0961f68bb") // node -> phone

    // Configuration service (JSON/CLI lines, same protocol as the USB cable).
    val CFG_SVC: UUID = UUID.fromString("f4e40001-7e5c-4b8a-9c1d-2a4f4c454b00")
    val CFG_RX: UUID = UUID.fromString("f4e40002-7e5c-4b8a-9c1d-2a4f4c454b00")
    val CFG_TX: UUID = UUID.fromString("f4e40003-7e5c-4b8a-9c1d-2a4f4c454b00")

    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}

data class FoundDevice(val address: String, val name: String, val rssi: Int)

sealed class LinkState {
    data object Idle : LinkState()
    data object Scanning : LinkState()
    data class Connecting(val step: String) : LinkState()
    data class Ready(val name: String, val hasConfig: Boolean, val mtu: Int, val services: List<String> = emptyList()) : LinkState()
    data class Failed(val reason: String) : LinkState()
}

/**
 * GATT client for the T-Echo. Android runs one GATT operation at a time, so every write and
 * descriptor write goes through [ops] and the next one starts from the completion callback.
 * All state changes run on [handler] (one thread), which keeps the queue free of races.
 */
@SuppressLint("MissingPermission") // callers check the runtime permissions first
class TechoLink(private val ctx: Context) {
    private val tag = "TechoLink"
    private val thread = HandlerThread("techo-ble").apply { start() }
    private val handler = Handler(thread.looper)
    private val adapter: BluetoothAdapter? =
        (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    private val _state = MutableStateFlow<LinkState>(LinkState.Idle)
    val state: StateFlow<LinkState> = _state.asStateFlow()

    private val _found = MutableStateFlow<List<FoundDevice>>(emptyList())
    val found: StateFlow<List<FoundDevice>> = _found.asStateFlow()

    private val _kissFrames = MutableSharedFlow<ByteArray>(extraBufferCapacity = 64)
    /** AX.25 frames heard by the node's radio. */
    val kissFrames: SharedFlow<ByteArray> = _kissFrames.asSharedFlow()

    private val _cfgLines = MutableSharedFlow<String>(extraBufferCapacity = 64)
    /** Text lines from the configuration channel (JSON replies, CLI text). */
    val cfgLines: SharedFlow<String> = _cfgLines.asSharedFlow()

    private val kissDecoder = fr.f4lek.techoaprs.proto.Kiss.Decoder { _kissFrames.tryEmit(it) }
    private val lineBuf = StringBuilder()

    private var gatt: BluetoothGatt? = null
    private var device: BluetoothDevice? = null
    private var mtu = 23
    private var wanted = false // the user wants to stay connected (auto-reconnect)
    private var cacheRefreshed = false // GATT cache already flushed during this connection
    private var retries = 0

    private sealed class Op {
        class Write(val ch: BluetoothGattCharacteristic, val data: ByteArray) : Op()
        class Notify(val ch: BluetoothGattCharacteristic) : Op()
        class Done(val ready: LinkState.Ready) : Op() // setup finished: notifications are on
    }

    private val ops = ArrayDeque<Op>()
    private var busy = false
    private var refusals = 0

    val isReady: Boolean get() = _state.value is LinkState.Ready
    val hasConfigChannel: Boolean get() = (_state.value as? LinkState.Ready)?.hasConfig == true
    val bluetoothOn: Boolean get() = adapter?.isEnabled == true

    // ---------------------------------------------------------------- scanning
    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val d = result.device
            val name = result.scanRecord?.deviceName ?: d.name ?: d.address
            val list = _found.value.filter { it.address != d.address } + FoundDevice(d.address, name, result.rssi)
            _found.value = list.sortedByDescending { it.rssi }
        }

        override fun onScanFailed(errorCode: Int) {
            _state.value = LinkState.Failed(L.s(R.string.link_scan_failed, errorCode))
        }
    }

    fun startScan() = handler.post {
        val sc = adapter?.bluetoothLeScanner ?: run {
            _state.value = LinkState.Failed(L.s(R.string.link_bt_off)); return@post
        }
        _found.value = emptyList()
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(TechoUuids.KISS_SVC)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        sc.startScan(listOf(filter), settings, scanCb)
        if (gatt == null) _state.value = LinkState.Scanning
        handler.postDelayed({ stopScanInternal() }, 15_000)
    }

    fun stopScan() = handler.post { stopScanInternal() }

    private fun stopScanInternal() {
        runCatching { adapter?.bluetoothLeScanner?.stopScan(scanCb) }
        if (_state.value is LinkState.Scanning) _state.value = LinkState.Idle
    }

    // ---------------------------------------------------------------- connection
    fun connect(address: String) = handler.post {
        stopScanInternal()
        wanted = true
        retries = 0
        val d = runCatching { adapter?.getRemoteDevice(address) }.getOrNull() ?: run {
            _state.value = LinkState.Failed(L.s(R.string.link_bad_address)); return@post
        }
        device = d
        startConnect()
    }

    fun disconnect() = handler.post {
        wanted = false
        handler.removeCallbacksAndMessages(RECONNECT)
        closeGatt()
        _state.value = LinkState.Idle
    }

    private fun startConnect() {
        val d = device ?: return
        closeGatt()
        // The node's characteristics (and their CCCDs) need an encrypted link when a PIN is set:
        // pair first, so the PIN dialog shows up once and not in the middle of the setup.
        if (d.bondState != BluetoothDevice.BOND_BONDED) {
            _state.value = LinkState.Connecting(L.s(R.string.link_pairing))
            registerBondReceiver()
            if (d.bondState == BluetoothDevice.BOND_NONE && !d.createBond()) {
                _state.value = LinkState.Failed(L.s(R.string.link_pairing_refused))
            }
            return
        }
        _state.value = LinkState.Connecting(L.s(R.string.link_connecting))
        gatt = d.connectGatt(ctx, false, gattCb, BluetoothDevice.TRANSPORT_LE)
    }

    private var bondReceiverOn = false
    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            val d: BluetoothDevice? = if (Build.VERSION.SDK_INT >= 33)
                i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
            else @Suppress("DEPRECATION") i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
            if (d?.address != device?.address) return
            when (i.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)) {
                BluetoothDevice.BOND_BONDED -> handler.post { if (wanted) startConnect() }
                BluetoothDevice.BOND_NONE -> handler.post {
                    if (wanted) _state.value = LinkState.Failed(L.s(R.string.link_pairing_failed))
                    wanted = false
                }
            }
        }
    }

    private fun registerBondReceiver() {
        if (bondReceiverOn) return
        val f = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) ctx.registerReceiver(bondReceiver, f, Context.RECEIVER_EXPORTED)
        else ctx.registerReceiver(bondReceiver, f)
        bondReceiverOn = true
    }

    private fun closeGatt() {
        ops.clear()
        busy = false
        gatt?.let { runCatching { it.disconnect() }; runCatching { it.close() } }
        gatt = null
        lineBuf.setLength(0)
    }

    private fun scheduleReconnect(reason: String) {
        if (!wanted) {
            _state.value = LinkState.Idle
            return
        }
        retries++
        val delay = (2_000L * retries).coerceAtMost(30_000L)
        _state.value = LinkState.Connecting(L.s(R.string.link_retry, reason, (delay / 1000).toInt()))
        handler.postAtTime({ if (wanted) startConnect() }, RECONNECT, android.os.SystemClock.uptimeMillis() + delay)
    }

    private val gattCb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            handler.post {
                if (g != gatt) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                    cacheRefreshed = false
                    _state.value = LinkState.Connecting(L.s(R.string.link_mtu))
                    g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                    if (!g.requestMtu(247)) g.discoverServices()
                } else {
                    closeGatt()
                    scheduleReconnect(if (status == BluetoothGatt.GATT_SUCCESS) L.s(R.string.link_disconnected) else L.s(R.string.link_lost, status))
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            handler.post {
                if (g != gatt) return@post
                mtu = if (status == BluetoothGatt.GATT_SUCCESS) newMtu else 23
                _state.value = LinkState.Connecting(L.s(R.string.link_discovering))
                g.discoverServices()
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            handler.post {
                if (g != gatt) return@post
                val kiss = g.getService(TechoUuids.KISS_SVC)
                val kissTx = kiss?.getCharacteristic(TechoUuids.KISS_TX)
                if (kissTx == null) {
                    wanted = false
                    closeGatt()
                    _state.value = LinkState.Failed(L.s(R.string.link_not_techo))
                    return@post
                }
                if (g.getService(TechoUuids.CFG_SVC) == null && !cacheRefreshed) {
                    // Android keeps the service list of a paired device in cache, and the node
                    // never says its services changed: after a firmware update the phone would
                    // keep seeing the old list (KISS only). Flush the cache and look again.
                    cacheRefreshed = true
                    if (refreshCache(g)) {
                        _state.value = LinkState.Connecting(L.s(R.string.link_rereading))
                        handler.postDelayed({ if (g == gatt) g.discoverServices() }, 600)
                        return@post
                    }
                }
                enqueue(Op.Notify(kissTx))
                g.getService(TechoUuids.CFG_SVC)?.getCharacteristic(TechoUuids.CFG_TX)?.let { enqueue(Op.Notify(it)) }
                retries = 0
                val name = g.device.name ?: g.device.address
                val svcs = g.services.map { it.uuid.toString() }
                enqueue(Op.Done(LinkState.Ready(name, g.getService(TechoUuids.CFG_SVC) != null, mtu, svcs)))
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            handler.post {
                if (status != BluetoothGatt.GATT_SUCCESS) Log.w(tag, "CCCD ${d.characteristic.uuid} status $status")
                if (status == 5 || status == 15 || status == 137) {
                    // Insufficient authentication/encryption: the bond is stale (node reflashed?).
                    wanted = false
                    closeGatt()
                    _state.value = LinkState.Failed(L.s(R.string.link_encryption))
                    return@post
                }
                opDone()
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            handler.post { opDone() }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            onData(c.uuid, value)
        }

        @Deprecated("Deprecated in API 33")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            if (Build.VERSION.SDK_INT < 33) onData(c.uuid, c.value ?: return)
        }
    }

    private fun onData(uuid: UUID, value: ByteArray) {
        val copy = value.copyOf()
        handler.post {
            when (uuid) {
                TechoUuids.KISS_TX -> kissDecoder.feed(copy)
                TechoUuids.CFG_TX -> {
                    for (b in copy) {
                        val ch = (b.toInt() and 0xFF).toChar()
                        if (ch == '\n') {
                            val line = lineBuf.toString().trimEnd('\r')
                            lineBuf.setLength(0)
                            if (line.isNotEmpty()) _cfgLines.tryEmit(line)
                        } else if (lineBuf.length < 16_384) lineBuf.append(ch)
                    }
                }
            }
        }
    }

    /** Demo build only (screenshots): pretends a T-Echo is connected. */
    internal fun demoReady(name: String) = handler.post {
        _state.value = LinkState.Ready(name, true, 247, listOf(TechoUuids.KISS_SVC.toString(), TechoUuids.CFG_SVC.toString()))
    }

    /** BluetoothGatt.refresh() is hidden API, but it is the only way to drop a stale cache. */
    private fun refreshCache(g: BluetoothGatt): Boolean = runCatching {
        g.javaClass.getMethod("refresh").invoke(g) as Boolean
    }.getOrDefault(false)

    // ---------------------------------------------------------------- writes
    /** Sends a KISS-framed AX.25 frame to the node's radio. */
    fun sendKiss(kissBytes: ByteArray): Boolean = write(TechoUuids.KISS_SVC, TechoUuids.KISS_RX, kissBytes)

    /** Sends one protocol line (a newline is appended). */
    fun sendLine(line: String): Boolean =
        write(TechoUuids.CFG_SVC, TechoUuids.CFG_RX, (line + "\n").toByteArray(Charsets.UTF_8))

    private fun write(svc: UUID, chr: UUID, data: ByteArray): Boolean {
        val ch = gatt?.getService(svc)?.getCharacteristic(chr) ?: return false
        handler.post {
            val chunk = (mtu - 3).coerceIn(20, 244)
            var off = 0
            while (off < data.size) {
                val n = minOf(chunk, data.size - off)
                enqueue(Op.Write(ch, data.copyOfRange(off, off + n)))
                off += n
            }
        }
        return true
    }

    private fun enqueue(op: Op) {
        ops.addLast(op)
        if (!busy) runNext()
    }

    private fun opDone() {
        busy = false
        runNext()
    }

    private fun runNext() {
        val g = gatt ?: return
        val op = ops.removeFirstOrNull() ?: return
        busy = true
        val started = when (op) {
            is Op.Done -> {
                _state.value = op.ready
                busy = false
                runNext()
                return
            }
            is Op.Write -> {
                val type = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                if (Build.VERSION.SDK_INT >= 33) {
                    g.writeCharacteristic(op.ch, op.data, type) == BluetoothStatusCodes.SUCCESS
                } else {
                    @Suppress("DEPRECATION")
                    op.ch.writeType = type
                    @Suppress("DEPRECATION")
                    op.ch.value = op.data
                    @Suppress("DEPRECATION")
                    g.writeCharacteristic(op.ch)
                }
            }
            is Op.Notify -> {
                g.setCharacteristicNotification(op.ch, true)
                val d = op.ch.getDescriptor(TechoUuids.CCCD)
                if (d == null) false
                else if (Build.VERSION.SDK_INT >= 33) {
                    g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS
                } else {
                    @Suppress("DEPRECATION")
                    d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    @Suppress("DEPRECATION")
                    g.writeDescriptor(d)
                }
            }
        }
        if (started) {
            refusals = 0
        } else {
            // The stack refused the operation (busy or gone): retry shortly instead of stalling,
            // and give up on it after ~2 s so one bad write cannot block the queue for ever.
            busy = false
            if (++refusals < 40) ops.addFirst(op) else refusals = 0
            handler.postDelayed({ if (!busy) runNext() }, 50)
        }
    }

    private companion object {
        val RECONNECT = Any()
    }
}
