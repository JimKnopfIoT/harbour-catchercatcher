import QtQuick 2.6
import Sailfish.Silica 1.0

// A small tappable toggle chip used for the map overlay layers.
Rectangle {
    id: chip
    property alias text: label.text
    property bool active: false
    property color activeColor: Theme.highlightColor
    signal toggled()

    radius: height / 2
    height: label.implicitHeight + Theme.paddingSmall
    width: label.implicitWidth + 2 * Theme.paddingMedium
    color: active ? Qt.rgba(activeColor.r, activeColor.g, activeColor.b, 0.25)
                  : Qt.rgba(1, 1, 1, 0.08)
    border.width: 1
    border.color: active ? activeColor : Theme.secondaryColor

    Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: Theme.fontSizeExtraSmall
        color: chip.active ? Theme.primaryColor : Theme.secondaryColor
    }

    MouseArea {
        anchors.fill: parent
        onClicked: chip.toggled()
    }
}
