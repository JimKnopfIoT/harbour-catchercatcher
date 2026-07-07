import QtQuick 2.6
import Sailfish.Silica 1.0

Page {
    allowedOrientations: Orientation.All
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height
        VerticalScrollDecorator { }
        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingMedium
            bottomPadding: Theme.paddingLarge

            PageHeader { title: qsTr("Über Catcher Catcher") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                linkColor: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                text: qsTr(
                    "<p>Ein Detektor für verdächtiges Mobilfunkverhalten, in der Tradition " +
                    "der klassischen <i>IMSI-Catcher-Catcher</i>-Apps.</p>" +

                    "<p><b>Tier 1 (aktiv, ohne Root)</b> liest den Zellzustand über " +
                    "<tt>ofono</tt> (D-Bus) und meldet Verhaltensmuster, die auf der " +
                    "sichtbaren Ebene erkennbar sind:</p>" +
                    "<ul>" +
                    "<li>erzwungene Herabstufung von LTE auf 2G/3G</li>" +
                    "<li>Wechsel zu einer bisher ungesehenen Zelle</li>" +
                    "<li>LAC/TAC-Wechsel ohne Betreiberwechsel</li>" +
                    "<li>Serving-Zelle ohne sichtbare Nachbarzellen</li>" +
                    "</ul>" +

                    "<p><b>Grenzen:</b> Der endgültige Beweis für einen IMSI-Catcher — " +
                    "die Herabstufung der Verschlüsselung auf <b>A5/0</b> (keine " +
                    "Verschlüsselung) und die <b>stille SMS</b> — ist auf dieser Ebene " +
                    "<i>nicht</i> sichtbar. Diese Signale liegen im Baseband und werden " +
                    "über die Qualcomm-Diag-Schnittstelle ausgelesen.</p>" +

                    "<p><b>Tier 2 (Root, /dev/diag)</b> ist auf diesem Gerät technisch " +
                    "möglich (SELinux permissive, <tt>/dev/diag</tt> lesbar) und in " +
                    "Arbeit. Erst damit werden Cipher-Downgrade und stille SMS " +
                    "erkennbar — die Methode, die auch SnoopSnitch verwendet.</p>" +

                    "<p>Alle Auswertungen sind Heuristiken, keine Gewissheiten. " +
                    "Ein einzelner Treffer ist noch kein Beweis für Überwachung.</p>")
                onLinkActivated: Qt.openUrlExternally(link)
            }
        }
    }
}
