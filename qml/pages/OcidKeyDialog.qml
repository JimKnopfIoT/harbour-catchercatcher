import QtQuick 2.6
import Sailfish.Silica 1.0

// Enter / change the OpenCellID API key (needed for the "known towers" overlay).
Dialog {
    id: dialog
    canAccept: keyField.text.trim().length > 0

    onAccepted: ocid.apiKey = keyField.text.trim()

    Column {
        width: parent.width

        DialogHeader { title: qsTr("OpenCellID-Key") }

        TextField {
            id: keyField
            width: parent.width
            text: ocid.apiKey
            label: qsTr("API-Key")
            placeholderText: qsTr("API-Key von opencellid.org")
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            linkColor: Theme.highlightColor
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            text: qsTr("Kostenlosen Key holen: registriere dich auf " +
                       "<a href='https://opencellid.org'>opencellid.org</a> und " +
                       "kopiere unter „API keys“ deinen Schlüssel hierher. " +
                       "Frei: 5000 Abfragen/Tag.")
            onLinkActivated: Qt.openUrlExternally(link)
        }
    }
}
