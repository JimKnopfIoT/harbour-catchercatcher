import QtQuick 2.0
import Sailfish.Silica 1.0
import catchercatcher 1.0

Page {
    id: page
    property CellMonitor monitor
    allowedOrientations: Orientation.All

    function levelColor(l) {
        if (l >= 3) return "#ff5555"
        if (l === 2) return "#ffaa00"
        if (l === 1) return Theme.highlightColor
        return Theme.secondaryColor
    }

    SilicaListView {
        anchors.fill: parent
        model: monitor.events

        header: PageHeader { title: qsTr("Ereignisprotokoll") }

        PullDownMenu {
            MenuItem {
                text: qsTr("Protokoll leeren")
                onClicked: monitor.clearEvents()
            }
        }

        delegate: ListItem {
            contentHeight: row.height + Theme.paddingMedium
            Row {
                id: row
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                Label {
                    text: modelData.time
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    width: Theme.itemSizeSmall
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    width: Theme.paddingSmall; radius: width / 2
                    height: width
                    color: levelColor(modelData.level)
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    text: modelData.text
                    width: row.width - Theme.itemSizeSmall - 2 * Theme.paddingMedium - Theme.paddingSmall
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: modelData.level >= 2 ? levelColor(modelData.level)
                                                : Theme.primaryColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        ViewPlaceholder {
            enabled: monitor.events.length === 0
            text: qsTr("Noch keine Ereignisse")
        }

        VerticalScrollDecorator { }
    }
}
