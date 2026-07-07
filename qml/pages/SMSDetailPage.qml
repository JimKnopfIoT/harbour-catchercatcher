import QtQuick 2.6
import Sailfish.Silica 1.0

// Full metadata of one captured SMS. Reached by tapping a row on the SMS page.
Page {
    id: page
    property var event: ({})
    allowedOrientations: Orientation.All

    function sevText(s) {
        if (s >= 3) return qsTr("kritisch")
        if (s === 2) return qsTr("verdächtig")
        if (s === 1) return qsTr("auffällig")
        return qsTr("info")
    }
    function val(k) {
        var v = event[k]
        return (v === undefined || v === null || v === "") ? "–" : String(v)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height
        VerticalScrollDecorator {}

        Column {
            id: col
            width: parent.width
            bottomPadding: Theme.paddingLarge

            PageHeader { title: qsTr("SMS-Metadaten") }

            DetailItem { label: qsTr("Typ"); value: val("type") }
            DetailItem { label: qsTr("Bewertung"); value: page.sevText(event.severity ? event.severity : 0) }
            DetailItem { label: qsTr("Empfangen"); value: val("ts") }
            DetailItem { label: qsTr("Gesendet"); value: val("sent") }
            DetailItem { label: qsTr("Absender"); value: val("from") }
            DetailItem { label: qsTr("SMSC"); value: val("smsc") }
            DetailItem { label: qsTr("TP-PID"); value: val("pid") }
            DetailItem { label: qsTr("TP-DCS"); value: val("dcs") }

            SectionHeader { text: qsTr("Inhalt") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                text: page.val("text")
            }
        }
    }
}
