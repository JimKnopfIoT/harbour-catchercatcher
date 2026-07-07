import QtQuick 2.6
import Sailfish.Silica 1.0

// Compact label/value tile for the 2-column cell-detail grid on the main page,
// so all fields (and the protection controls below them) fit on one screen.
Column {
    property alias label: lab.text
    property alias value: val.text
    spacing: 0

    Label {
        id: lab
        width: parent.width
        truncationMode: TruncationMode.Fade
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeTiny
    }
    Label {
        id: val
        width: parent.width
        truncationMode: TruncationMode.Fade
        color: Theme.primaryColor
        font.pixelSize: Theme.fontSizeSmall
    }
}
