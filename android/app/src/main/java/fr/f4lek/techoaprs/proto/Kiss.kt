package fr.f4lek.techoaprs.proto

import java.io.ByteArrayOutputStream

/**
 * KISS framing (same rules as firmware/src/kiss.cpp): FEND delimits, FESC escapes,
 * first body byte = port/command (0x00 = data on port 0).
 */
object Kiss {
    const val FEND = 0xC0
    const val FESC = 0xDB
    const val TFEND = 0xDC
    const val TFESC = 0xDD
    const val MAX_FRAME = 340

    fun encode(ax25: ByteArray): ByteArray {
        val out = ByteArrayOutputStream(ax25.size + 8)
        out.write(FEND)
        out.write(0x00)
        for (b in ax25) {
            when (val v = b.toInt() and 0xFF) {
                FEND -> { out.write(FESC); out.write(TFEND) }
                FESC -> { out.write(FESC); out.write(TFESC) }
                else -> out.write(v)
            }
        }
        out.write(FEND)
        return out.toByteArray()
    }

    /** Reassembles frames from a byte stream split across BLE notifications. */
    class Decoder(private val onFrame: (ByteArray) -> Unit) {
        private val buf = ByteArrayOutputStream()
        private var inFrame = false
        private var escaped = false
        private var overrun = false

        fun feed(data: ByteArray) {
            for (b in data) feed(b.toInt() and 0xFF)
        }

        private fun feed(v: Int) {
            if (v == FEND) {
                if (inFrame && !overrun && buf.size() > 1) {
                    val body = buf.toByteArray()
                    // Data frames on any port: low nibble 0.
                    if ((body[0].toInt() and 0x0F) == 0) onFrame(body.copyOfRange(1, body.size))
                }
                buf.reset()
                inFrame = true
                escaped = false
                overrun = false
                return
            }
            if (!inFrame) return
            val byte = if (escaped) {
                escaped = false
                when (v) {
                    TFEND -> FEND
                    TFESC -> FESC
                    else -> v
                }
            } else if (v == FESC) {
                escaped = true
                return
            } else v
            if (buf.size() >= MAX_FRAME + 1) overrun = true else buf.write(byte)
        }
    }
}
