package fr.f4lek.techoaprs.ui

import androidx.annotation.StringRes
import fr.f4lek.techoaprs.L
import fr.f4lek.techoaprs.R

/**
 * The node's configuration keys, grouped like the web configurator (web/index.html SCHEMA).
 * Ranges are the firmware's own (config.cpp): a value outside them makes the node reject the
 * whole `set`, so the form checks them first. Labels are string resources.
 */
sealed class Field(val key: String, @StringRes val label: Int, @StringRes val hint: Int? = null, val reboot: Boolean = false) {
    class Text(key: String, label: Int, val maxLen: Int = 63, hint: Int? = null, val upper: Boolean = false) :
        Field(key, label, hint)

    class IntF(key: String, label: Int, val min: Int, val max: Int, hint: Int? = null, reboot: Boolean = false) :
        Field(key, label, hint, reboot)

    class FloatF(key: String, label: Int, val min: Double, val max: Double, hint: Int? = null) : Field(key, label, hint)

    class Bool(key: String, label: Int, hint: Int? = null) : Field(key, label, hint)

    /** Options are (value, label): the label is a string resource id or a literal text. */
    class Choice(key: String, label: Int, val options: List<Pair<Number, Any>>, hint: Int? = null, reboot: Boolean = false) :
        Field(key, label, hint, reboot)

    /** Lat/lon pair with a "take the T-Echo GPS position" button. */
    class Position : Field("latitude", R.string.f_position)

    /** The 4 use profiles table (fixed, walking, bike, car). */
    class Profiles : Field("profiles", R.string.f_profiles)
}

/** Text of a [Field.Choice] option label. */
fun optionText(label: Any): String = if (label is Int) L.s(label) else label.toString()

data class SectionDef(@StringRes val title: Int, val fields: List<Field>)

val CONFIG_SCHEMA = listOf(
    SectionDef(
        R.string.s_station, listOf(
            Field.Text("callsign", R.string.f_callsign, 9, R.string.h_callsign, upper = true),
            Field.Choice("mode", R.string.f_mode, listOf(0 to R.string.o_mode_digi, 1 to R.string.o_mode_tracker, 2 to R.string.o_mode_both)),
            Field.Text("symbol", R.string.f_symbol, 1, R.string.h_symbol),
            Field.Text("overlay", R.string.f_overlay, 1, R.string.h_overlay),
            Field.Text("comment", R.string.f_comment, 63, R.string.h_comment),
            Field.Text("status", R.string.f_status, 63),
            Field.Text("msgText", R.string.f_msgText, 40, R.string.h_msgText),
        )
    ),
    SectionDef(
        R.string.s_paths, listOf(
            Field.Text("pathDigi", R.string.f_pathDigi, 31, R.string.h_path, upper = true),
            Field.Text("pathTracker", R.string.f_pathTracker, 31, R.string.h_path, upper = true),
            Field.Text("pathBoth", R.string.f_pathBoth, 31, R.string.h_path, upper = true),
            Field.Text("path", R.string.f_path, 31, R.string.h_path_fallback, upper = true),
        )
    ),
    SectionDef(
        R.string.s_beacons, listOf(
            Field.Position(),
            Field.IntF("beaconInterval", R.string.f_beaconInterval, 15, 240, R.string.h_beaconInterval),
            Field.IntF("posAmbiguity", R.string.f_posAmbiguity, 0, 4),
            Field.Bool("compressedPos", R.string.f_compressedPos),
        )
    ),
    SectionDef(
        R.string.s_tracker, listOf(
            Field.IntF("trackerIntervalSecs", R.string.f_trackerIntervalSecs, 10, 3600),
            Field.IntF("trackerMinDistanceM", R.string.f_trackerMinDistanceM, 0, 5000),
            Field.IntF("trackerMinSpacing", R.string.f_trackerMinSpacing, 10, 120, R.string.h_trackerMinSpacing),
            Field.Choice(
                "smartBeaconPreset", R.string.f_smartBeaconPreset,
                listOf(0 to R.string.o_preset_none, 1 to R.string.o_preset_walk, 2 to R.string.o_preset_bike, 3 to R.string.o_preset_car)
            ),
            Field.Bool("sendAltitude", R.string.f_sendAltitude),
            Field.Bool("trackerSleep", R.string.f_trackerSleep, R.string.h_trackerSleep),
            Field.Profiles(),
        )
    ),
    SectionDef(
        R.string.s_digi, listOf(
            Field.Choice("digiMode", R.string.f_digiMode, listOf(0 to R.string.o_off, 1 to "WIDE1-1", 2 to "WIDE1-1 + WIDE2-n")),
            Field.Bool("cadActive", R.string.f_cadActive),
            Field.Text("blacklist", R.string.f_blacklist, 63, R.string.h_blacklist, upper = true),
            Field.Bool("txDisabled", R.string.f_txDisabled),
            Field.Bool("gpsInDigi", R.string.f_gpsInDigi),
            Field.Bool("gpsEco", R.string.f_gpsEco),
        )
    ),
    SectionDef(
        R.string.s_radio, listOf(
            Field.IntF("frequency", R.string.f_frequency, 430_000_000, 928_000_000, R.string.h_frequency, reboot = true),
            Field.IntF("spreadingFactor", R.string.f_spreadingFactor, 5, 12, reboot = true),
            Field.IntF("codingRate4", R.string.f_codingRate4, 5, 8, reboot = true),
            Field.Choice(
                "signalBandwidth", R.string.f_signalBandwidth,
                listOf(62.5 to "62.5", 125.0 to "125", 250.0 to "250", 500.0 to "500"), reboot = true
            ),
            Field.IntF("power", R.string.f_power, 2, 22, R.string.h_power),
        )
    ),
    SectionDef(
        R.string.s_sensors, listOf(
            Field.Bool("wxSensorActive", R.string.f_wxSensorActive),
            Field.Bool("sendBatteryTelemetry", R.string.f_sendBatteryTelemetry),
            Field.IntF("telemetryIntervalMin", R.string.f_telemetryIntervalMin, 0, 720, R.string.h_interval_15),
            Field.IntF("wxIntervalMin", R.string.f_wxIntervalMin, 0, 720, R.string.h_interval_15),
            Field.IntF("heightCorrection", R.string.f_heightCorrection, -500, 5000),
            Field.FloatF("temperatureCorrection", R.string.f_temperatureCorrection, -5.0, 5.0),
            Field.FloatF("chipTempOffset", R.string.f_chipTempOffset, -10.0, 10.0),
        )
    ),
    SectionDef(
        R.string.s_screen, listOf(
            Field.Choice(
                "epdRotation", R.string.f_epdRotation,
                listOf(1 to R.string.o_rot_1, 2 to R.string.o_rot_2, 3 to R.string.o_rot_3, 0 to R.string.o_rot_0)
            ),
            Field.Bool("sceneAutoAdvance", R.string.f_sceneAutoAdvance),
            Field.IntF("screenTimeoutSecs", R.string.f_screenTimeoutSecs, 0, 3600),
            Field.Bool("popups", R.string.f_popups),
            Field.Text("sleepMsg", R.string.f_sleepMsg, 63, R.string.h_sleepMsg),
        )
    ),
    SectionDef(
        R.string.s_power, listOf(
            Field.IntF("sleepCutMv", R.string.f_sleepCutMv, 2500, 4200),
            Field.IntF("sleepWakeMv", R.string.f_sleepWakeMv, 2600, 4500),
            Field.FloatF("batteryCal", R.string.f_batteryCal, 0.8, 1.2, R.string.h_batteryCal),
        )
    ),
    SectionDef(
        R.string.s_remote, listOf(
            Field.IntF("msgRetries", R.string.f_msgRetries, 0, 5, R.string.h_msgRetries),
            Field.Bool("remoteEnabled", R.string.f_remoteEnabled),
            Field.Text("managers", R.string.f_managers, 63, R.string.h_managers, upper = true),
            Field.Bool("queriesEnabled", R.string.f_queriesEnabled),
            Field.Choice("tncProtocol", R.string.f_tncProtocol, listOf(0 to R.string.o_off, 1 to R.string.o_tnc2, 2 to "KISS")),
        )
    ),
    SectionDef(
        R.string.s_bluetooth, listOf(
            Field.Bool("bleEnabled", R.string.f_bleEnabled, R.string.h_bleEnabled),
            Field.Text("blePin", R.string.f_blePin, 6, R.string.h_blePin),
        )
    ),
)

val PROFILE_NAMES = listOf(R.string.prof_fixed, R.string.prof_walk, R.string.prof_bike, R.string.prof_car)
