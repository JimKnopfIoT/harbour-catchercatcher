import QtQuick 2.6
import Sailfish.Silica 1.0
import catchercatcher 1.0

/*
 * WhitelistPage — view and edit the list of cells the user has marked
 * trustworthy. Trusted cells (MCC-MNC-LAC-CID) are never flagged. Long-press
 * an entry (or use its context menu) to remove it.
 */
Page {
    id: page
    property CellMonitor monitor
    allowedOrientations: Orientation.All

    SilicaListView {
        id: list
        anchors.fill: parent
        model: monitor.whitelist

        header: Column {
            width: list.width
            bottomPadding: Theme.paddingMedium
            PageHeader { title: qsTr("Whitelist") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Vertrauenswürdige Zellen (MCC-MNC-LAC-Cell-ID). Für diese " +
                           "wird kein Alarm ausgelöst. Langes Antippen → Eintrag löschen.")
            }
        }

        PullDownMenu {
            MenuItem {
                text: qsTr("Alle entfernen")
                enabled: monitor.whitelist.length > 0
                onClicked: remorse.execute(qsTr("Whitelist leeren"), function() {
                    var all = monitor.whitelist.slice()
                    for (var i = 0; i < all.length; ++i)
                        monitor.removeFromWhitelist(all[i])
                })
            }
        }

        RemorsePopup { id: remorse }

        delegate: ListItem {
            id: item
            width: list.width
            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Aus Whitelist entfernen")
                    onClicked: item.remove(modelData)
                }
            }
            function remove(key) {
                remorseAction(qsTr("Entfernt"), function() {
                    monitor.removeFromWhitelist(key)
                })
            }
            onPressAndHold: openMenu()
            Label {
                anchors {
                    left: parent.left; leftMargin: Theme.horizontalPageMargin
                    right: parent.right; rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: modelData
                truncationMode: TruncationMode.Fade
                color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
        }

        ViewPlaceholder {
            enabled: monitor.whitelist.length === 0
            text: qsTr("Whitelist leer")
            hintText: qsTr("Auf der Hauptseite „Zelle vertrauen“ wählen, um " +
                           "die aktuelle Zelle aufzunehmen.")
        }

        VerticalScrollDecorator {}
    }
}
