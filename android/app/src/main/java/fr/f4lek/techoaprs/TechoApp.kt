package fr.f4lek.techoaprs

import android.app.Application

class TechoApp : Application() {
    lateinit var techo: Techo
        private set

    override fun onCreate() {
        super.onCreate()
        L.init(this)
        techo = Techo(this)
        // Sample data for screenshots: the class only exists in the "demo" build type.
        runCatching {
            Class.forName("fr.f4lek.techoaprs.DemoData").getMethod("install", Techo::class.java).invoke(null, techo)
        }
    }

    /** Called by the language setting: new strings everywhere, then the activity is recreated. */
    fun setLanguage(lang: String) {
        L.set(this, lang)
        techo.createChannels()
    }
}
