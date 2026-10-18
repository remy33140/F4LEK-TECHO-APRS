package fr.f4lek.techoaprs

import android.app.Application

class TechoApp : Application() {
    lateinit var techo: Techo
        private set

    override fun onCreate() {
        super.onCreate()
        L.init(this)
        techo = Techo(this)
    }

    /** Called by the language setting: new strings everywhere, then the activity is recreated. */
    fun setLanguage(lang: String) {
        L.set(this, lang)
        techo.createChannels()
    }
}
