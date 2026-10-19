package fr.f4lek.techoaprs

import fr.f4lek.techoaprs.data.SummitDb
import fr.f4lek.techoaprs.proto.Aprs
import fr.f4lek.techoaprs.proto.AprsMessage
import fr.f4lek.techoaprs.proto.Ax25
import fr.f4lek.techoaprs.proto.Kiss
import fr.f4lek.techoaprs.proto.TextFrame
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ProtoTest {
    @Test
    fun kissRoundTripWithEscapes() {
        val body = byteArrayOf(1, 0xC0.toByte(), 2, 0xDB.toByte(), 3)
        val enc = Kiss.encode(body)
        assertArrayEquals(
            byteArrayOf(0xC0.toByte(), 0, 1, 0xDB.toByte(), 0xDC.toByte(), 2, 0xDB.toByte(), 0xDD.toByte(), 3, 0xC0.toByte()),
            enc
        )
        val got = mutableListOf<ByteArray>()
        val dec = Kiss.Decoder { got += it }
        // Split across "notifications" like BLE does.
        dec.feed(enc.copyOfRange(0, 4))
        dec.feed(enc.copyOfRange(4, enc.size))
        assertEquals(1, got.size)
        assertArrayEquals(body, got[0])
    }

    @Test
    fun ax25RoundTrip() {
        val f = TextFrame.parse("F4LEK-7>APL2OY,F1ABC-1*,WIDE2-1:!4246.43N/00008.83W[ test")!!
        val ax = Ax25.encode(f)!!
        val back = Ax25.decode(ax)!!
        assertEquals("F4LEK-7>APL2OY,F1ABC-1*,WIDE2-1:!4246.43N/00008.83W[ test", back.toString())
    }

    @Test
    fun ax25RejectsLongCall() {
        assertNull(Ax25.encode(TextFrame("EA2/F4LEK", "APL2OY", emptyList(), "x")))
    }

    @Test
    fun parseMessageWithId() {
        val m = Aprs.parseMessage(TextFrame.parse("F1ABC-9>APDR16,WIDE1-1::F4LEK-7  :Salut depuis le col{42")!!)
        assertEquals(AprsMessage.Text("F1ABC-9", "F4LEK-7", "Salut depuis le col", "42"), m)
    }

    @Test
    fun parseAckAndReplyAck() {
        val a = Aprs.parseMessage(TextFrame.parse("APRS2SOTA>APRS::F4LEK-7  :ack017")!!)
        assertEquals(AprsMessage.Ack("APRS2SOTA", "F4LEK-7", "017", false), a)
        val r = Aprs.parseMessage(TextFrame.parse("F1ABC>APRS::F4LEK    :ok{AB}12")!!) as AprsMessage.Text
        assertEquals("ok", r.text)
        assertEquals("AB", r.id)
    }

    @Test
    fun thirdPartyIsUnwrapped() {
        val m = Aprs.parseMessage(TextFrame.parse("F5XYZ-10>APLRG1,WIDE1-1:}APRS2SOTA>APRS,TCPIP,F5XYZ-10*::F4LEK-7  :Spotted{12")!!)
        assertTrue(m is AprsMessage.Text && m.from == "APRS2SOTA" && m.text == "Spotted")
    }

    @Test
    fun sotaSpotText() {
        assertEquals("F4LEK/P", Aprs.portableCall("F4LEK-7"))
        assertEquals(
            "F/PO-001 14.062MHz CW F4LEK/P QRV",
            Aprs.sotaSpot("F/PO-001", "14.062", "CW", "F4LEK/P", "QRV")
        )
        assertEquals("F/PO-001 145.500MHz FM EA2/F4LEK/P", Aprs.sotaSpot("F/PO-001", "145.500", "FM", "EA2/F4LEK/P", " "))
    }

    @Test
    fun summitSearchNormalisation() {
        assertEquals("pic d anie", SummitDb.normalize("Pic d'Anie"))
        assertEquals("vignemale", SummitDb.normalize("Vignemale"))
        assertEquals("pyrenees ouest", SummitDb.normalize("Pyrénées Ouest"))
        assertEquals("fpo001", SummitDb.compactCode("F/PO-001"))
        val f = SummitDb.parseCsvLine("""F/PO-019,France,"Pyrénées Ouest","Petit ""V"", Vignemale",3032""")
        assertEquals(listOf("F/PO-019", "France", "Pyrénées Ouest", "Petit \"V\", Vignemale", "3032"), f)
    }

    @Test
    fun gatedAprs2SotaRepliesSurviveKissAndAx25() {
        // What the T-Echo now forwards: the iGate's outer frame, inner header kept in the info.
        for ((line, expected) in listOf(
            "F5XYZ-10>APLRG1,WIDE1-1:}APRS2SOTA>APRS,TCPIP,F5XYZ-10*::F4LEK-7  :ack017" to
                AprsMessage.Ack("APRS2SOTA", "F4LEK-7", "017", false),
            "F5XYZ-10>APLRG1,WIDE1-1:}APRS2SOTA>APRS,TCPIP,F5XYZ-10*::F4LEK-7  :Spotted F/PO-001 14.062 CW{AB" to
                AprsMessage.Text("APRS2SOTA", "F4LEK-7", "Spotted F/PO-001 14.062 CW", "AB"),
        )) {
            val got = mutableListOf<ByteArray>()
            Kiss.Decoder { got += it }.feed(Kiss.encode(Ax25.encode(TextFrame.parse(line)!!)!!))
            val frame = Ax25.decode(got.single())!!
            assertEquals(expected, Aprs.parseMessage(frame))
        }
    }

    @Test
    fun aprsIsPasscode() {
        assertEquals(13023, fr.f4lek.techoaprs.net.AprsIsLink.passcode("N0CALL"))
        // The SSID does not change the passcode; case does not matter.
        assertEquals(
            fr.f4lek.techoaprs.net.AprsIsLink.passcode("F4LEK"),
            fr.f4lek.techoaprs.net.AprsIsLink.passcode("f4lek-6")
        )
    }

    @Test
    fun aprsIsCallAndServer() {
        val l = fr.f4lek.techoaprs.net.AprsIsLink
        assertTrue(l.validCall("F4LEK-6") && l.validCall("F4LEK") && l.validCall("EA2ABC-15"))
        assertTrue(!l.validCall("N0CALL-2") && !l.validCall("F4LEK-16") && !l.validCall("F4LEK/P") && !l.validCall(""))
        assertEquals("rotate.aprs2.net" to 14580, l.splitServer("rotate.aprs2.net"))
        assertEquals("10.0.2.2" to 14581, l.splitServer("10.0.2.2:14581"))
    }

    @Test
    fun aprsIsLinesParse() {
        // A message gated from RF, as APRS-IS delivers it (q-construct in the path).
        val f = TextFrame.parse("F4LEK-7>APL2OY,WIDE1-1,qAR,F5XYZ-10::F4LEK-6  :Bien arrive au sommet{021")!!
        assertEquals(AprsMessage.Text("F4LEK-7", "F4LEK-6", "Bien arrive au sommet", "021"), Aprs.parseMessage(f))
        // What the app sends back: an ack in TNC2 form.
        val ack = TextFrame("F4LEK-6", fr.f4lek.techoaprs.net.AprsIsLink.TOCALL, listOf("TCPIP*"), ":%-9s:ack%s".format("F4LEK-7", "021"))
        assertEquals("F4LEK-6>APZLEK,TCPIP*::F4LEK-7  :ack021", ack.toString())
    }
}
