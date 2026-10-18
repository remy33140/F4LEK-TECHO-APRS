package fr.f4lek.techoaprs.proto

/** APRS message packet (APRS101 chapter 14): ":ADDRESSEE:text{id". */
sealed class AprsMessage {
    abstract val from: String
    abstract val to: String

    data class Text(override val from: String, override val to: String, val text: String, val id: String?) :
        AprsMessage()

    data class Ack(override val from: String, override val to: String, val id: String, val rejected: Boolean) :
        AprsMessage()
}

object Aprs {
    const val SOTA_GATEWAY = "APRS2SOTA"

    /** Peels third-party headers ("}SRC>DST,PATH:info") so gated messages are seen too. */
    private fun unwrap(f: TextFrame): TextFrame {
        var cur = f
        repeat(3) {
            if (!cur.info.startsWith("}")) return cur
            cur = TextFrame.parse(cur.info.substring(1)) ?: return cur
        }
        return cur
    }

    fun parseMessage(frame: TextFrame): AprsMessage? {
        val f = unwrap(frame)
        val info = f.info
        if (info.length < 11 || info[0] != ':' || info[10] != ':') return null
        val to = info.substring(1, 10).trim()
        var body = info.substring(11).trimEnd('\r', '\n')
        val from = f.src
        if (to.startsWith("BLN")) return null // bulletins are not conversations
        val lower = body.lowercase()
        if ((lower.startsWith("ack") || lower.startsWith("rej")) && body.length in 4..8 && !body.contains(' ')) {
            val id = body.substring(3).substringBefore('}')
            return AprsMessage.Ack(from, to, id, lower.startsWith("rej"))
        }
        var id: String? = null
        val brace = body.lastIndexOf('{')
        if (brace >= 0 && body.length - brace in 2..7) {
            // Reply-ack form "{MM}AA": keep the message number before '}'.
            id = body.substring(brace + 1).substringBefore('}')
            body = body.substring(0, brace)
        }
        return AprsMessage.Text(from, to, body, id)
    }

    /** "F4LEK-7" and "f4lek-7" are the same station; "F4LEK-0" is "F4LEK". */
    fun sameCall(a: String, b: String): Boolean = norm(a) == norm(b)

    fun norm(c: String): String = c.trim().uppercase().removeSuffix("-0")

    /** Callsign + "/P" for portable operation (firmware baseCallOf, sota.cpp). */
    fun portableCall(nodeCall: String): String {
        val c = nodeCall.ifBlank { "NOCALL" }
        val base = c.substringBefore('-')
        return if (base.contains('/')) base else "$base/P"
    }

    /** Message line sent to APRS2SOTA: "<ref> <freq>MHz <mode> <call> [comment]". */
    fun sotaSpot(ref: String, freqMHz: String, mode: String, call: String, comment: String): String =
        buildString {
            append(ref).append(' ').append(freqMHz).append("MHz ").append(mode).append(' ').append(call)
            if (comment.isNotBlank()) append(' ').append(comment.trim())
        }
}
