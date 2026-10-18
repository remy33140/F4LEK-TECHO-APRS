package fr.f4lek.techoaprs

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import androidx.annotation.PluralsRes
import androidx.annotation.StringRes
import java.util.Locale

/**
 * App language (English by default, French, or the phone's language) and localized strings for
 * code that has no Compose context: link states, errors, notifications, view models.
 */
object L {
    const val SYSTEM = "system"
    val LANGS = listOf("en", "fr", SYSTEM)

    private const val PREF = "lang"
    @Volatile private var res: Resources? = null

    fun lang(ctx: Context): String =
        ctx.getSharedPreferences("techo", Context.MODE_PRIVATE).getString(PREF, "en") ?: "en"

    /** [base] with the chosen language applied (unchanged for "phone language"). */
    fun wrap(base: Context): Context {
        val lang = lang(base)
        if (lang == SYSTEM) return base
        val locale = Locale.forLanguageTag(lang)
        Locale.setDefault(locale)
        val conf = Configuration(base.resources.configuration)
        conf.setLocale(locale)
        return base.createConfigurationContext(conf)
    }

    fun init(app: Context) {
        res = wrap(app).resources
    }

    /** Stores the choice; the caller recreates the activity so the screens pick it up. */
    fun set(app: Context, lang: String) {
        app.getSharedPreferences("techo", Context.MODE_PRIVATE).edit().putString(PREF, lang).apply()
        if (lang == SYSTEM) Locale.setDefault(app.resources.configuration.locales[0])
        init(app)
    }

    fun s(@StringRes id: Int, vararg args: Any): String = res!!.getString(id, *args)

    fun q(@PluralsRes id: Int, n: Int): String = res!!.getQuantityString(id, n, n)
}
