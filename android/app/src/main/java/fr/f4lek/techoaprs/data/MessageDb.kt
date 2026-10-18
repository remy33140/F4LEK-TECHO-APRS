package fr.f4lek.techoaprs.data

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class MsgState(val code: Int) {
    SENDING(0), SENT(1), ACKED(2), REJECTED(3), FAILED(4), RECEIVED(5);

    companion object {
        fun of(c: Int) = entries.firstOrNull { it.code == c } ?: FAILED
    }
}

data class ChatMessage(
    val rowId: Long,
    val peer: String,
    val outgoing: Boolean,
    val text: String,
    val msgId: String?,
    val state: MsgState,
    val time: Long,
    val note: String?,
)

data class Conversation(val peer: String, val last: ChatMessage, val unread: Int)

/** APRS conversations, one per correspondent callsign. */
class MessageDb(ctx: Context) : SQLiteOpenHelper(ctx, "messages.db", null, 1) {
    /** Bumped on every change, so screens can re-query. */
    private val _version = MutableStateFlow(0L)
    val version: StateFlow<Long> = _version.asStateFlow()

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            """CREATE TABLE msg(id INTEGER PRIMARY KEY AUTOINCREMENT, peer TEXT NOT NULL, outgoing INTEGER,
               text TEXT, msgid TEXT, state INTEGER, ts INTEGER, unread INTEGER DEFAULT 0, note TEXT)"""
        )
        db.execSQL("CREATE INDEX msg_peer ON msg(peer, ts)")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {}

    private fun changed() { _version.value = _version.value + 1 }

    fun addOutgoing(peer: String, text: String): Long {
        val id = writableDatabase.insert("msg", null, ContentValues().apply {
            put("peer", peer.uppercase()); put("outgoing", 1); put("text", text)
            put("state", MsgState.SENDING.code); put("ts", System.currentTimeMillis())
        })
        changed()
        return id
    }

    fun update(rowId: Long, state: MsgState, msgId: String? = null, note: String? = null) {
        writableDatabase.update("msg", ContentValues().apply {
            put("state", state.code)
            if (msgId != null) put("msgid", msgId)
            if (note != null) put("note", note)
        }, "id=?", arrayOf("$rowId"))
        changed()
    }

    /**
     * Stores a received message unless it is a retry of one already stored (APRS senders repeat
     * until acked: same id, or same text within 10 minutes when there is no id).
     * Returns false for a duplicate.
     */
    fun addIncoming(peer: String, text: String, msgId: String?): Boolean {
        val p = peer.uppercase()
        val since = System.currentTimeMillis() - 10 * 60_000
        val dup = if (msgId != null)
            readableDatabase.rawQuery(
                "SELECT 1 FROM msg WHERE peer=? AND outgoing=0 AND msgid=? AND text=? AND ts>? LIMIT 1",
                arrayOf(p, msgId, text, "${System.currentTimeMillis() - 60 * 60_000}")
            )
        else readableDatabase.rawQuery(
            "SELECT 1 FROM msg WHERE peer=? AND outgoing=0 AND text=? AND ts>? LIMIT 1", arrayOf(p, text, "$since")
        )
        if (dup.use { it.moveToFirst() }) return false
        writableDatabase.insert("msg", null, ContentValues().apply {
            put("peer", p); put("outgoing", 0); put("text", text); put("msgid", msgId)
            put("state", MsgState.RECEIVED.code); put("ts", System.currentTimeMillis()); put("unread", 1)
        })
        changed()
        return true
    }

    /** An ack/rej from [peer] for our message number [msgId]. Returns the text acked, if any. */
    fun ack(peer: String, msgId: String, rejected: Boolean): String? {
        val c = readableDatabase.rawQuery(
            "SELECT id,text FROM msg WHERE peer=? AND outgoing=1 AND msgid=? ORDER BY ts DESC LIMIT 1",
            arrayOf(peer.uppercase(), msgId.trimStart('0').ifEmpty { "0" }.padStart(3, '0'))
        )
        val hit = c.use { if (it.moveToFirst()) it.getLong(0) to it.getString(1) else null } ?: return null
        update(hit.first, if (rejected) MsgState.REJECTED else MsgState.ACKED)
        return hit.second
    }

    /** Messages still waiting for an ack after [timeoutMs] become "no ack". */
    fun expireSent(timeoutMs: Long) {
        val n = writableDatabase.update(
            "msg", ContentValues().apply { put("state", MsgState.FAILED.code); putNull("note") },
            "outgoing=1 AND state=? AND ts<?", arrayOf("${MsgState.SENT.code}", "${System.currentTimeMillis() - timeoutMs}")
        )
        if (n > 0) changed()
    }

    fun conversations(): List<Conversation> {
        val sql = """SELECT m.id,m.peer,m.outgoing,m.text,m.msgid,m.state,m.ts,m.note,
                     (SELECT COUNT(*) FROM msg u WHERE u.peer=m.peer AND u.unread=1)
                     FROM msg m WHERE m.id IN (SELECT MAX(id) FROM msg GROUP BY peer) ORDER BY m.ts DESC"""
        return readableDatabase.rawQuery(sql, null).use { c ->
            buildList { while (c.moveToNext()) add(Conversation(c.getString(1), row(c), c.getInt(8))) }
        }
    }

    fun messages(peer: String): List<ChatMessage> = readableDatabase.rawQuery(
        "SELECT id,peer,outgoing,text,msgid,state,ts,note FROM msg WHERE peer=? ORDER BY ts", arrayOf(peer.uppercase())
    ).use { c -> buildList { while (c.moveToNext()) add(row(c)) } }

    fun lastFrom(peer: String): ChatMessage? = readableDatabase.rawQuery(
        "SELECT id,peer,outgoing,text,msgid,state,ts,note FROM msg WHERE peer=? AND outgoing=0 ORDER BY ts DESC LIMIT 1",
        arrayOf(peer.uppercase())
    ).use { c -> if (c.moveToFirst()) row(c) else null }

    fun markRead(peer: String) {
        val n = writableDatabase.update("msg", ContentValues().apply { put("unread", 0) },
            "peer=? AND unread=1", arrayOf(peer.uppercase()))
        if (n > 0) changed()
    }

    fun deleteConversation(peer: String) {
        writableDatabase.delete("msg", "peer=?", arrayOf(peer.uppercase()))
        changed()
    }

    private fun row(c: android.database.Cursor) = ChatMessage(
        c.getLong(0), c.getString(1), c.getInt(2) == 1, c.getString(3) ?: "", c.getString(4),
        MsgState.of(c.getInt(5)), c.getLong(6), c.getString(7)
    )
}
