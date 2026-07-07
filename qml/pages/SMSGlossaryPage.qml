import QtQuick 2.6
import Sailfish.Silica 1.0

// Leftmost page of the navigation strip and the app's stack root. Explains the
// SMS types and abbreviations. On first launch it builds the rest of the strip
// as a chain of *attached* pages and navigates forward to land on the main page:
//   [SMS-Begriffe(this)] -> [SMS] -> [Main] -> (Main attaches Map -> Begriffe)
// Attached pages persist across back/forward swipes (unlike push()ed ones).
Page {
    allowedOrientations: Orientation.All
    property var monitor
    property bool _built: false

    // Build the strip step by step: attach SMS, then navigate to it. SMS then
    // attaches Main and advances, Main attaches Map, Map attaches the glossary —
    // each hop in its own onStatusChanged so no navigation races the next
    // pushAttached (doing it all synchronously in onCompleted left us stuck here).
    onStatusChanged: {
        if (status === PageStatus.Active && !_built) {
            _built = true
            pageStack.pushAttached(Qt.resolvedUrl("SMSPage.qml"), { monitor: monitor })
            // Qt 5.6 has no Qt.callLater; navigate synchronously. The one-shot
            // _built guard makes the resulting status re-entry harmless (QML only
            // logs a benign "binding loop" note, the hop happens exactly once).
            pageStack.navigateForward(PageStackAction.Immediate)
        }
    }
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height
        VerticalScrollDecorator {}

        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingMedium
            bottomPadding: Theme.paddingLarge

            PageHeader { title: qsTr("SMS — Begriffe") }

            SectionHeader { text: qsTr("Grundlagen") }
            DetailBlock {
                term: qsTr("TP-PID (Protocol Identifier)")
                body: qsTr("Sagt dem Telefon, wie eine SMS zu behandeln ist. Bestimmte " +
                    "Werte kennzeichnen Sondertypen — z. B. 0x40 = „Type 0“ (still), " +
                    "0x41–0x47 = „ersetze frühere Nachricht“, 0x7F = Daten für die SIM.")
            }
            DetailBlock {
                term: qsTr("TP-DCS (Data Coding Scheme)")
                body: qsTr("Legt Kodierung und Klasse fest: Text (7-bit), 8-bit-Binär, " +
                    "UCS2 — und die Message-Class (0 = Flash, 1 = normal, 2 = SIM, " +
                    "3 = Endgerät).")
            }
            DetailBlock {
                term: qsTr("SMSC")
                body: qsTr("Die Vermittlungsstelle (SMS Service Center), über die die " +
                    "Nachricht kam. Ein unerwartetes SMSC ist ein Warnzeichen.")
            }

            SectionHeader { text: qsTr("Auffällige Typen") }
            DetailBlock {
                term: qsTr("Stille SMS (Type 0) — kritisch")
                body: qsTr("TP-PID 0x40. Wird vom Telefon quittiert, aber weder angezeigt " +
                    "noch gespeichert. Dient zur heimlichen Ortung/Präsenzprüfung — " +
                    "das klassische Tracking-Signal.")
            }
            DetailBlock {
                term: qsTr("Binär-/OTA-SMS — kritisch")
                body: qsTr("8-bit-Daten-SMS, oft an einen bestimmten Port adressiert. " +
                    "Trägt Konfigurationen oder Applets — kann z. B. Einstellungen " +
                    "unterschieben.")
            }
            DetailBlock {
                term: qsTr("SIM-OTA / (U)SIM-Update — kritisch")
                body: qsTr("SMS mit Daten für die SIM-Karte (TP-PID 0x7F bzw. DCS-Klasse 2). " +
                    "Damit kann ein Angreifer Applets/Parameter auf der SIM „over the air“ " +
                    "ändern — der einzige realistische „Firmware-artige“ Fern-Eingriff.")
            }
            DetailBlock {
                term: qsTr("Flash-SMS (Class 0) — verdächtig")
                body: qsTr("DCS-Klasse 0: erscheint sofort am Display, ohne gespeichert zu " +
                    "werden. Für Phishing/Schock-Nachrichten missbraucht.")
            }
            DetailBlock {
                term: qsTr("Replace Short Message — verdächtig")
                body: qsTr("TP-PID 0x41–0x47: ersetzt eine frühere Nachricht desselben Typs — " +
                    "kann genutzt werden, um Spuren zu überschreiben.")
            }
            DetailBlock {
                term: qsTr("WAP-Push / MMS-Notification — auffällig")
                body: qsTr("Port-adressierte SMS (z. B. 2948), die einen Download/eine URL " +
                    "anstößt. Meist legitim, kann aber Einfallstor sein.")
            }
            DetailBlock {
                term: qsTr("MWI / Voicemail-Indikator — info")
                body: qsTr("Message-Waiting-Indication. Normalerweise harmlos, aber " +
                    "manipulierbar (falsche Symbole setzen).")
            }

            SectionHeader { text: qsTr("Geräte-Identität") }
            DetailBlock {
                term: qsTr("IMEI")
                body: qsTr("International Mobile Equipment Identity — die eindeutige " +
                    "Seriennummer deines Modems/Telefons (15 Stellen). Identifiziert das " +
                    "Gerät im Netz, unabhängig von der SIM.")
            }
            DetailBlock {
                term: qsTr("SIM ICCID")
                body: qsTr("Integrated Circuit Card Identifier — die aufgedruckte " +
                    "Seriennummer der SIM-Karte (bis 20 Stellen). Eindeutig pro physischer " +
                    "SIM. Ändert sie sich unerwartet, wurde die SIM getauscht.")
            }
            DetailBlock {
                term: qsTr("SIM IMSI")
                body: qsTr("International Mobile Subscriber Identity — deine geheime " +
                    "Teilnehmerkennung im Netz (MCC+MNC+MSIN). Genau die Kennung, die ein " +
                    "IMSI-Catcher abgreifen will. Eine Änderung bedeutet neue SIM/Profil.")
            }
            DetailBlock {
                term: qsTr("Warnung bei Änderung")
                body: qsTr("Die App merkt sich ICCID/IMSI/Firmware und warnt beim nächsten " +
                    "Start, wenn sich etwas geändert hat — Hinweis auf SIM-Tausch oder ein " +
                    "Firmware-Flash. Quittieren im Pulley der SMS-Seite.")
            }

            SectionHeader { text: qsTr("Bewertung & Speicherung") }
            DetailBlock {
                term: qsTr("Stufen")
                body: qsTr("info · auffällig · verdächtig · kritisch (roter Punkt). Jede " +
                    "erfasste SMS wird sofort mit Zeit, Zelle und Position gespeichert " +
                    "und lässt sich per Pulley als CSV exportieren.")
            }
            DetailBlock {
                term: qsTr("Grenze")
                body: qsTr("Diese Erkennung braucht Tier 2 (Baseband über /dev/diag). " +
                    "Ohne aktiven Collector bleibt die Liste leer.")
            }
        }
    }
}
