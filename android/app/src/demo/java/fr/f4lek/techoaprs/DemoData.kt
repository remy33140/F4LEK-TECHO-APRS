package fr.f4lek.techoaprs

import fr.f4lek.techoaprs.data.MsgState
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Screenshot build only: a connected T-Echo with plausible data, fed through the app's real
 * code paths (protocol lines for config/status, AX.25 frames for received messages).
 * Callsigns are placeholders (N0CALL), not real stations.
 */
object DemoData {
    private const val CALL = "N0CALL-7"

    @JvmStatic
    fun install(t: Techo) {
        t.prefs.edit().putString("sota.call", "N0CALL/P").putString("callsign", CALL).apply()
        t.scope.launch {
            delay(800)
            t.link.demoReady("T-Echo $CALL")
            delay(500)
            t.node.demoLine("""{"ok":true,"config":$CONFIG,"persisted":true}""")
            delay(500)
            t.node.demoLine("""{"ok":true,"status":$STATUS}""")
            if (t.messages.conversations().isEmpty()) seedMessages(t)
        }
    }

    private suspend fun seedMessages(t: Techo) {
        val spot = t.messages.addOutgoing("APRS2SOTA", "F/PO-001 14.062MHz CW N0CALL/P QRV")
        t.messages.update(spot, MsgState.SENT, "017")
        delay(300)
        t.demoFrame("N0CALL-10>APLRG1,WIDE1-1:}APRS2SOTA>APRS,TCPIP,N0CALL-10*::${CALL.padEnd(9)}:ack017")
        delay(300)
        t.demoFrame("N0CALL-10>APLRG1,WIDE1-1:}APRS2SOTA>APRS,TCPIP,N0CALL-10*::${CALL.padEnd(9)}:Spotted N0CALL/P on F/PO-001 14.062 CW{AB")
        delay(300)
        t.demoFrame("N0CALL-9>APDR16,WIDE1-1::${CALL.padEnd(9)}:Heard you 599 on 14.062, GL on the summit!{12")
        delay(300)
        val reply = t.messages.addOutgoing("N0CALL-9", "Tnx! QRT in 20 min, 73")
        t.messages.update(reply, MsgState.SENT, "018")
        delay(300)
        t.demoFrame("N0CALL-9>APDR16,WIDE1-1::${CALL.padEnd(9)}:ack018")
    }

    private const val CONFIG = """{"callsign":"N0CALL-7","tocall":"APL2OY","path":"WIDE1-1","pathDigi":"WIDE1-1",
"pathTracker":"WIDE1-1,WIDE2-1","pathBoth":"WIDE1-1,WIDE2-1","comment":"SOTA with the T-Echo","status":"","msgText":"",
"msgRetries":3,"sotaRecent":"F","sotaLast":"F/PO-001,","sotaPlan":"F/PO-001,,014062,CW","overlay":"/","posAmbiguity":0,
"compressedPos":false,"symbol":"[","beaconInterval":30,"latitude":0,"longitude":0,"digiMode":0,"blacklist":"",
"frequency":433775000,"spreadingFactor":12,"codingRate4":5,"signalBandwidth":125,"power":22,"cadActive":true,
"sendBatteryTelemetry":true,"wxSensorActive":true,"telemetryIntervalMin":53,"wxIntervalMin":55,"heightCorrection":0,
"temperatureCorrection":0,"chipTempOffset":-3,"sleepCutMv":3200,"sleepWakeMv":3400,"txDisabled":false,
"remoteEnabled":false,"tncProtocol":0,"tncMode":false,"managers":"","bleEnabled":true,"blePin":"123456","mode":1,
"epdRotation":1,"sleepMsg":"","sceneAutoAdvance":true,"screenTimeoutSecs":0,"popups":true,"trackerIntervalSecs":120,
"trackerMinDistanceM":150,"trackerMinSpacing":30,"smartBeaconPreset":1,"sendAltitude":true,"gpsEco":false,
"trackerSleep":false,"gpsInDigi":false,"profiles":[{"ssid":0,"slowSec":0,"fastSec":0,"distM":0,"symbol":"/#"},
{"ssid":7,"slowSec":0,"fastSec":0,"distM":0,"symbol":"/["},{"ssid":8,"slowSec":0,"fastSec":0,"distM":0,"symbol":"/b"},
{"ssid":5,"slowSec":0,"fastSec":0,"distM":0,"symbol":"/>"}],"queriesEnabled":false}"""

    private const val STATUS = """{"version":"1.0alpha","build":"b109","board":"T-Echo","uptimeMs":5400000,
"radio":{"module":"SX1262","state":"RX","powerDbm":22,"err":0,"rxCount":142,"txCount":23,"digiCount":0,
"lastRssi":-97.5,"lastSnr":6.25,"rxlog":false},"sensors":{"wx":true,"tempC":8.4,"hum":61,"hPa":702.3,
"vbatDivV":3.98,"ina":false},"display":true,
"gps":{"present":true,"fix":true,"sats":9,"inView":12,"hdop":0.9,"lat":42.77389,"lon":-0.14722,"altM":3298,
"speedKmh":0,"courseDeg":0,"timeValid":true}}"""
}
