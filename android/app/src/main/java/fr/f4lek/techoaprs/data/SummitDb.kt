package fr.f4lek.techoaprs.data

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import java.io.BufferedReader
import java.io.InputStream
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.text.Normalizer
import java.time.LocalDate
import java.util.zip.GZIPInputStream
import kotlin.math.asin
import kotlin.math.cos
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.sqrt

data class Summit(
    val code: String,      // "F/PO-001"
    val assoc: String,     // "F"
    val region: String,    // "PO"
    val name: String,      // "Vignemale"
    val altM: Int,
    val lat: Double,
    val lon: Double,
    val points: Int,
    val bonus: Int,
    val activations: Int,
    val regionName: String = "",
    val distanceKm: Double? = null,
)

data class Named(val code: String, val name: String)

/** Official SOTA summit list in SQLite (bundled snapshot, refreshable from sota.org.uk). */
class SummitDb(ctx: Context) : SQLiteOpenHelper(ctx, "summits.db", null, 1) {
    private val appCtx = ctx.applicationContext

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            """CREATE TABLE summit(code TEXT PRIMARY KEY, ncode TEXT, assoc TEXT, region TEXT, num INTEGER,
               name TEXT, norm TEXT, alt INTEGER, lat REAL, lon REAL, pts INTEGER, bonus INTEGER, acts INTEGER)"""
        )
        db.execSQL("CREATE INDEX summit_ar ON summit(assoc, region, num)")
        db.execSQL("CREATE INDEX summit_lat ON summit(lat)")
        db.execSQL("CREATE TABLE assoc(code TEXT PRIMARY KEY, name TEXT)")
        db.execSQL("CREATE TABLE region(assoc TEXT, code TEXT, name TEXT, PRIMARY KEY(assoc, code))")
        db.execSQL("CREATE TABLE meta(k TEXT PRIMARY KEY, v TEXT)")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {}

    fun count(): Int = readableDatabase.rawQuery("SELECT COUNT(*) FROM summit", null).use { c ->
        if (c.moveToFirst()) c.getInt(0) else 0
    }

    fun listDate(): String? = readableDatabase.rawQuery("SELECT v FROM meta WHERE k='date'", null).use { c ->
        if (c.moveToFirst()) c.getString(0) else null
    }

    /** First launch: load the bundled snapshot (a few seconds). */
    fun ensureLoaded(progress: (Int) -> Unit) {
        if (count() > 0) return
        appCtx.assets.open(ASSET).use { import(GZIPInputStream(it), progress) }
    }

    /** Downloads the current official list and replaces the local one. */
    fun downloadAndImport(progress: (Int) -> Unit) {
        val conn = URL(LIST_URL).openConnection() as HttpURLConnection
        conn.connectTimeout = 15_000
        conn.readTimeout = 30_000
        try {
            if (conn.responseCode != 200) throw IllegalStateException("HTTP ${conn.responseCode}")
            conn.inputStream.use { import(it, progress) }
        } finally {
            conn.disconnect()
        }
    }

    private fun import(input: InputStream, progress: (Int) -> Unit) {
        val r = BufferedReader(InputStreamReader(input, Charsets.UTF_8), 1 shl 16)
        val title = r.readLine() ?: throw IllegalStateException(fr.f4lek.techoaprs.L.s(fr.f4lek.techoaprs.R.string.err_empty_list))
        r.readLine() // header
        val date = Regex("Date=([0-9/]+)").find(title)?.groupValues?.get(1) ?: ""
        val today = LocalDate.now()
        val db = writableDatabase
        db.beginTransaction()
        try {
            db.delete("summit", null, null)
            db.delete("assoc", null, null)
            db.delete("region", null, null)
            val ins = db.compileStatement(
                "INSERT OR REPLACE INTO summit(code,ncode,assoc,region,num,name,norm,alt,lat,lon,pts,bonus,acts) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"
            )
            val assocs = HashMap<String, String>()
            val regions = HashMap<String, String>()
            var n = 0
            while (true) {
                val line = r.readLine() ?: break
                val f = parseCsvLine(line)
                if (f.size < 15) continue
                if (!validOn(f[13], today)) continue
                val code = f[0]
                val slash = code.indexOf('/')
                val dash = code.lastIndexOf('-')
                if (slash <= 0 || dash <= slash) continue
                val a = code.substring(0, slash)
                val reg = code.substring(slash + 1, dash)
                assocs.putIfAbsent(a, f[1])
                regions.putIfAbsent("$a/$reg", f[2])
                ins.clearBindings()
                ins.bindString(1, code)
                ins.bindString(2, compactCode(code))
                ins.bindString(3, a)
                ins.bindString(4, reg)
                ins.bindLong(5, code.substring(dash + 1).toLongOrNull() ?: 0)
                ins.bindString(6, f[3])
                ins.bindString(7, normalize(f[3]))
                ins.bindLong(8, f[4].toLongOrNull() ?: 0)
                ins.bindDouble(9, f[9].toDoubleOrNull() ?: 0.0)
                ins.bindDouble(10, f[8].toDoubleOrNull() ?: 0.0)
                ins.bindLong(11, f[10].toLongOrNull() ?: 0)
                ins.bindLong(12, f[11].toLongOrNull() ?: 0)
                ins.bindLong(13, f[14].toLongOrNull() ?: 0)
                ins.executeInsert()
                if (++n % 5000 == 0) progress(n)
            }
            for ((k, v) in assocs) db.insert("assoc", null, ContentValues().apply { put("code", k); put("name", v) })
            for ((k, v) in regions) db.insert("region", null, ContentValues().apply {
                put("assoc", k.substringBefore('/')); put("code", k.substringAfter('/')); put("name", v)
            })
            db.execSQL("INSERT OR REPLACE INTO meta(k,v) VALUES('date',?)", arrayOf(date))
            db.setTransactionSuccessful()
            progress(n)
        } finally {
            db.endTransaction()
        }
    }

    // ---------------------------------------------------------------- queries
    fun associations(): List<Named> = readableDatabase.rawQuery("SELECT code,name FROM assoc ORDER BY code", null)
        .use { c -> buildList { while (c.moveToNext()) add(Named(c.getString(0), c.getString(1))) } }

    fun regions(assoc: String): List<Named> =
        readableDatabase.rawQuery("SELECT code,name FROM region WHERE assoc=? ORDER BY code", arrayOf(assoc))
            .use { c -> buildList { while (c.moveToNext()) add(Named(c.getString(0), c.getString(1))) } }

    fun summitsIn(assoc: String, region: String): List<Summit> =
        query("WHERE s.assoc=? AND s.region=? ORDER BY s.num", arrayOf(assoc, region))

    fun byCode(code: String): Summit? = query("WHERE s.code=?", arrayOf(code.uppercase())).firstOrNull()

    /**
     * Instant search as the user types: a reference ("F/PO-001", "po-1", "po001") or words of the
     * name, accents and case ignored ("vign" -> F/PO-001 Vignemale). [preferAssoc] goes first.
     */
    fun search(q: String, preferAssoc: String?, assoc: String? = null, region: String? = null, limit: Int = 40): List<Summit> {
        val raw = q.trim()
        if (raw.isEmpty()) return emptyList()
        val scope = StringBuilder()
        val scopeArgs = mutableListOf<String>()
        if (assoc != null) { scope.append(" AND s.assoc=?"); scopeArgs += assoc }
        if (region != null) { scope.append(" AND s.region=?"); scopeArgs += region }
        val pref = preferAssoc ?: ""
        val out = LinkedHashMap<String, Summit>()

        // 1) As a reference, when it looks like one (no spaces, has a digit or a slash).
        if (!raw.contains(' ') && (raw.any { it.isDigit() } || raw.contains('/'))) {
            // "po-1" means PO-001 first, then PO-1xx: the zero-padded form goes first.
            val variants = linkedSetOf<String>()
            Regex("^(.*?)(\\d{1,2})$").find(compactCode(raw))?.let { m ->
                variants += m.groupValues[1] + m.groupValues[2].padStart(3, '0')
            }
            variants += compactCode(raw)
            for (v in variants) {
                query(
                    "WHERE (s.ncode LIKE ? OR s.ncode LIKE ?)$scope ORDER BY (s.ncode LIKE ?) DESC, (s.assoc=?) DESC, s.ncode LIMIT $limit",
                    arrayOf("$v%", "%$v%") + scopeArgs + arrayOf("$v%", pref)
                ).forEach { out.putIfAbsent(it.code, it) }
            }
        }
        // 2) As words of the name: every word must appear; a word start ranks first.
        val words = normalize(raw).split(' ').filter { it.isNotEmpty() }
        if (words.isNotEmpty() && out.size < limit) {
            val where = words.joinToString(" AND ") { "s.norm LIKE ?" }
            val args = words.map { "%$it%" }
            val first = words.first()
            query(
                "WHERE $where$scope ORDER BY (s.norm LIKE ? OR s.norm LIKE ?) DESC, (s.assoc=?) DESC, s.pts DESC, s.alt DESC LIMIT $limit",
                (args + scopeArgs + listOf("$first%", "% $first%", pref)).toTypedArray()
            ).forEach { out.putIfAbsent(it.code, it) }
        }
        return out.values.take(limit)
    }

    /** Summits around a position, nearest first. */
    fun nearby(lat: Double, lon: Double, radiusKm: Double = 25.0, limit: Int = 30): List<Summit> {
        val dLat = radiusKm / 111.0
        val dLon = radiusKm / (111.0 * cos(Math.toRadians(lat)).coerceAtLeast(0.05))
        return query(
            "WHERE s.lat BETWEEN ? AND ? AND s.lon BETWEEN ? AND ?",
            arrayOf("${lat - dLat}", "${lat + dLat}", "${lon - dLon}", "${lon + dLon}")
        ).map { it.copy(distanceKm = haversineKm(lat, lon, it.lat, it.lon)) }
            .filter { it.distanceKm!! <= radiusKm }
            .sortedBy { it.distanceKm }
            .take(limit)
    }

    private fun query(tail: String, args: Array<String>): List<Summit> = readableDatabase.rawQuery(
        "SELECT s.code,s.assoc,s.region,s.name,s.alt,s.lat,s.lon,s.pts,s.bonus,s.acts,r.name FROM summit s " +
            "LEFT JOIN region r ON r.assoc=s.assoc AND r.code=s.region $tail", args
    ).use { c ->
        buildList {
            while (c.moveToNext()) add(
                Summit(
                    c.getString(0), c.getString(1), c.getString(2), c.getString(3), c.getInt(4),
                    c.getDouble(5), c.getDouble(6), c.getInt(7), c.getInt(8), c.getInt(9), c.getString(10) ?: ""
                )
            )
        }
    }

    companion object {
        const val LIST_URL = "https://storage.sota.org.uk/summitslist.csv"

        /** Gzipped CSV. Not named *.gz: the Android build unpacks those and drops the extension. */
        const val ASSET = "summits.sota"

        private val marks = Regex("\\p{Mn}+")
        private val nonAlnum = Regex("[^a-z0-9]+")

        /** "Pic d'Anie" -> "pic d anie" (no accents, lower case, single spaces). */
        fun normalize(s: String): String =
            Normalizer.normalize(s, Normalizer.Form.NFD).replace(marks, "").lowercase()
                .replace(nonAlnum, " ").trim()

        /** "F/PO-001" -> "fpo001". */
        fun compactCode(s: String): String = s.lowercase().replace(nonAlnum, "")

        fun haversineKm(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
            val dLat = Math.toRadians(lat2 - lat1)
            val dLon = Math.toRadians(lon2 - lon1)
            val a = sin(dLat / 2).pow(2) + cos(Math.toRadians(lat1)) * cos(Math.toRadians(lat2)) * sin(dLon / 2).pow(2)
            return 2 * 6371.0 * asin(sqrt(a))
        }

        private fun validOn(ddmmyyyy: String, today: LocalDate): Boolean {
            val p = ddmmyyyy.split('/')
            if (p.size != 3) return false
            return runCatching { !LocalDate.of(p[2].toInt(), p[1].toInt(), p[0].toInt()).isBefore(today) }
                .getOrDefault(false)
        }

        fun parseCsvLine(line: String): List<String> {
            val out = ArrayList<String>(17)
            val sb = StringBuilder()
            var quoted = false
            var i = 0
            while (i < line.length) {
                val c = line[i]
                if (quoted) {
                    if (c == '"') {
                        if (i + 1 < line.length && line[i + 1] == '"') { sb.append('"'); i++ } else quoted = false
                    } else sb.append(c)
                } else when (c) {
                    '"' -> quoted = true
                    ',' -> { out += sb.toString(); sb.setLength(0) }
                    else -> sb.append(c)
                }
                i++
            }
            out += sb.toString()
            return out
        }
    }
}
