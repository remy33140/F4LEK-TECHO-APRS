package fr.f4lek.techoaprs.proto

import java.io.ByteArrayOutputStream

/** A frame in TNC2 text form: "SRC>DST,PATH:info". */
data class TextFrame(val src: String, val dst: String, val path: List<String>, val info: String) {
    override fun toString(): String =
        buildString {
            append(src).append('>').append(dst)
            for (p in path) append(',').append(p)
            append(':').append(info)
        }

    companion object {
        fun parse(line: String): TextFrame? {
            val gt = line.indexOf('>')
            val colon = line.indexOf(':')
            if (gt <= 0 || colon <= gt + 1) return null
            val header = line.substring(gt + 1, colon).split(',')
            return TextFrame(line.substring(0, gt), header[0], header.drop(1), line.substring(colon + 1))
        }
    }
}

/** AX.25 UI frames <-> TNC2 text (mirror of firmware/src/ax25.cpp). */
object Ax25 {
    private const val CONTROL_UI = 0x03
    private const val PID_NO_L3 = 0xF0

    fun decode(frame: ByteArray): TextFrame? {
        if (frame.size < 16) return null
        val addrs = mutableListOf<String>()
        var i = 0
        var last = false
        while (!last) {
            if (i + 7 > frame.size || addrs.size >= 10) return null
            val sb = StringBuilder()
            for (k in 0 until 6) {
                val c = (frame[i + k].toInt() and 0xFF) shr 1
                if (c != ' '.code) sb.append(c.toChar())
            }
            val ssidByte = frame[i + 6].toInt() and 0xFF
            val ssid = (ssidByte shr 1) and 0x0F
            if (ssid != 0) sb.append('-').append(ssid)
            // H bit only means "repeated" on digipeater addresses (index >= 2).
            if (addrs.size >= 2 && (ssidByte and 0x80) != 0) sb.append('*')
            addrs += sb.toString()
            last = (ssidByte and 0x01) != 0
            i += 7
        }
        if (addrs.size < 2 || i + 2 > frame.size) return null
        if ((frame[i].toInt() and 0xFF) != CONTROL_UI || (frame[i + 1].toInt() and 0xFF) != PID_NO_L3) return null
        val info = String(frame, i + 2, frame.size - i - 2, Charsets.ISO_8859_1)
        // Only the last repeated digipeater keeps its '*', as TNC2 text does.
        val path = addrs.drop(2).toMutableList()
        val lastRepeated = path.indexOfLast { it.endsWith('*') }
        for (k in path.indices) if (k != lastRepeated) path[k] = path[k].removeSuffix("*")
        return TextFrame(addrs[1], addrs[0], path, info)
    }

    fun encode(f: TextFrame): ByteArray? {
        val out = ByteArrayOutputStream()
        val all = listOf(f.dst, f.src) + f.path
        for ((idx, a) in all.withIndex()) {
            val repeated = a.endsWith('*')
            val addr = a.removeSuffix("*")
            val dash = addr.indexOf('-')
            val call = (if (dash >= 0) addr.substring(0, dash) else addr).uppercase()
            val ssid = if (dash >= 0) addr.substring(dash + 1).toIntOrNull() ?: return null else 0
            if (call.isEmpty() || call.length > 6 || ssid !in 0..15) return null
            if (!call.all { it in 'A'..'Z' || it in '0'..'9' }) return null
            for (k in 0 until 6) out.write((if (k < call.length) call[k].code else ' '.code) shl 1)
            var s = 0x60 or (ssid shl 1)
            if (idx == 0) s = s or 0x80 // command frame: C bit on destination
            if (idx >= 2 && repeated) s = s or 0x80
            if (idx == all.lastIndex) s = s or 0x01
            out.write(s)
        }
        out.write(CONTROL_UI)
        out.write(PID_NO_L3)
        out.write(f.info.toByteArray(Charsets.ISO_8859_1))
        return out.toByteArray()
    }
}
