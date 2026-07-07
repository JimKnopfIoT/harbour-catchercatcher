import QtQuick 2.6
import Sailfish.Silica 1.0
import catchercatcher 1.0

/*
 * GlossaryPage — attached to the right of MainPage (swipe left to reveal).
 * Explains every value on the dashboard and how to judge it.
 */
Page {
    id: page
    property CellMonitor monitor
    allowedOrientations: Orientation.All

    // ofono reports RSRP/RSRQ as 3GPP indices, not dBm. Convert for the reader.
    function rsrpDbm(idx) { return idx >= 0 ? (idx - 140) : NaN }      // 0..97 → -140..-44 dBm
    function rsrqDb(idx)  { return idx >= 0 ? (idx / 2 - 19.5) : NaN } // 0..34 → -19.5..-2.5 dB

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        VerticalScrollDecorator {}

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium
            bottomPadding: Theme.paddingLarge

            PageHeader { title: qsTr("Begriffe & Bewertung") }

            SectionHeader { text: qsTr("Netz & Zelle") }
            DetailBlock {
                term: qsTr("Technik (2G/3G/4G/5G)")
                body: qsTr("Die aktuelle Funkgeneration. Wichtigstes Warnsignal: ein " +
                    "erzwungenes Herunterstufen von LTE/5G auf 2G (GSM). Die meisten " +
                    "IMSI-Catcher zwingen das Telefon auf 2G, weil dort die " +
                    "Verschlüsselung schwach oder abschaltbar ist. Gegenmittel: „Nur LTE“.")
            }
            DetailBlock {
                term: qsTr("Cell ID / eNodeB")
                body: qsTr("Eindeutige Kennung der Funkzelle. Bei LTE stecken darin die " +
                    "eNodeB-(Mast-)Nummer und die Zellnummer. Eine plötzlich neue, " +
                    "unbekannte Cell ID mit sehr starkem Signal ist verdächtig — " +
                    "besonders, wenn sie in keiner Zell-Datenbank (OpenCellID) auftaucht.")
            }
            DetailBlock {
                term: qsTr("LAC / TAC")
                body: qsTr("Location / Tracking Area Code — die Funk-Region. Häufige " +
                    "Wechsel des LAC/TAC ohne Betreiberwechsel oder ohne echte Bewegung " +
                    "können ein Catcher-Trick sein, um das Telefon zum Anmelden (und " +
                    "IMSI-Senden) zu zwingen.")
            }
            DetailBlock {
                term: qsTr("EARFCN")
                body: qsTr("Nummer des LTE-Funkkanals ⇒ Frequenzband. Ein Betreiber, der " +
                    "plötzlich auf einem ungewöhnlichen Band/EARFCN sendet, kann ein " +
                    "Hinweis sein.")
            }

            SectionHeader { text: qsTr("Signalstärke") }
            DetailBlock {
                term: qsTr("RSRP" + (monitor.rsrp >= 0 ? "  (≈ " + page.rsrpDbm(monitor.rsrp).toFixed(0) + " dBm)" : ""))
                body: qsTr("Empfangsleistung des Referenzsignals. Angezeigt als 3GPP-Index " +
                    "(0…97); als Leistung ≈ Index − 140 dBm. Faustregel:\n" +
                    "• > −80 dBm: ausgezeichnet\n" +
                    "• −80 … −95: gut\n" +
                    "• −95 … −110: mäßig\n" +
                    "• < −110: schwach.\n" +
                    "Ein neuer Mast, der viel stärker ist als alles andere in der Nähe, " +
                    "ist ein Warnzeichen (Catcher stehen oft nah und senden laut).")
            }
            DetailBlock {
                term: qsTr("RSRQ" + (monitor.rsrq >= 0 ? "  (≈ " + page.rsrqDb(monitor.rsrq).toFixed(1) + " dB)" : ""))
                body: qsTr("Empfangsqualität. Index 0…34; als Verhältnis ≈ Index/2 − 19,5 dB. " +
                    "> −10 dB gut, −10…−15 mäßig, < −15 schlecht.")
            }
            DetailBlock {
                term: qsTr("Signal %")
                body: qsTr("Grobe, vom Modem gelieferte Balken-Prozentzahl. Nur als " +
                    "Anhaltspunkt; RSRP/RSRQ sind aussagekräftiger.")
            }
            DetailBlock {
                term: qsTr("Nachbarzellen")
                body: qsTr("Wie viele Nachbarzellen sichtbar sind. Echte Netze haben " +
                    "praktisch immer Nachbarn. Eine starke Zelle, die dauerhaft 0 " +
                    "Nachbarn zeigt, ist verdächtig — Catcher stehen oft „allein“.")
            }

            SectionHeader { text: qsTr("Bewertung & Schutz") }
            DetailBlock {
                term: qsTr("Bedrohungsstufe")
                body: qsTr("0 unauffällig · 1 Hinweis · 2 Warnung · 3 Alarm. Ergibt sich " +
                    "aus den Heuristiken. Ein einzelner Treffer ist noch kein Beweis — " +
                    "erst mehrere zusammen (z. B. 2G-Downgrade + unbekannte Zelle + keine " +
                    "Nachbarn) sind ernst zu nehmen.")
            }
            DetailBlock {
                term: qsTr("Whitelist")
                body: qsTr("Von dir als vertrauenswürdig markierte Zellen (z. B. zu Hause, " +
                    "am Arbeitsplatz). Zellen außerhalb der Whitelist werden markiert.")
            }
            DetailBlock {
                term: qsTr("Funktechnik-Präferenz / „Nur LTE“")
                body: qsTr("Zwingt das Modem auf LTE und sperrt so die 2G-Downgrade-" +
                    "Catcher aus. „Auto-Schutz“ macht das bei Alarm automatisch. " +
                    "„Modem trennen“ meldet das Telefon ganz ab.")
            }
            DetailBlock {
                term: qsTr("Grenzen (wichtig)")
                body: qsTr("Cipher-Downgrade (A5/0) und stille SMS sind auf dieser Ebene " +
                    "unsichtbar — sie brauchen die Baseband-Analyse (Tier 2, /dev/diag, " +
                    "nur auf Qualcomm-Geräten). Alle Werte hier sind Heuristiken, keine " +
                    "Gewissheiten.")
            }
        }
    }
}
