import QtQuick 2.6
import Sailfish.Silica 1.0
import catchercatcher 1.0

/*
 * SMSPage — reached by swiping up (PushUpMenu) on the main page. Lists all
 * suspicious SMS types the Tier 2 /dev/diag collector surfaces, rates them,
 * and persists them. Swipe left for the term glossary.
 */
Page {
    id: page
    property CellMonitor monitor
    // SIM/IMEI identity is hidden by default so it doesn't end up in every
    // screenshot; reveal it via the pulley when you actually need it.
    property bool showSim: false
    property bool _mainAttached: false
    allowedOrientations: Orientation.All

    // second hop of the strip build: on first activation (during startup, when
    // the root navigated forward to here) attach the main page and advance to it,
    // so the app lands on Main with SMS persistently to its left.
    onStatusChanged: {
        if (status === PageStatus.Active && !_mainAttached) {
            _mainAttached = true
            pageStack.pushAttached(Qt.resolvedUrl("MainPage.qml"), { monitor: monitor })
            // synchronous (Qt 5.6 has no Qt.callLater); the _mainAttached guard
            // keeps the status re-entry a one-shot, benign binding-loop note.
            pageStack.navigateForward(PageStackAction.Immediate)
        }
    }

    // traffic-light rating: green = harmless, yellow/orange = suspicious,
    // red = definite attack / manipulation
    function sevColor(s) {
        if (s >= 3) return "#ff5555"   // kritisch — erkannter Angriff/Manipulation
        if (s === 2) return "#ff8800"  // verdächtig
        if (s === 1) return "#ffd000"  // auffällig
        return "#4caf50"               // unkritisch
    }
    function sevLabel(s) {
        if (s >= 3) return qsTr("kritisch")
        if (s === 2) return qsTr("verdächtig")
        if (s === 1) return qsTr("auffällig")
        return qsTr("unkritisch")
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: monitor.smsEvents

        header: Column {
            width: list.width
            spacing: Theme.paddingSmall
            bottomPadding: Theme.paddingMedium

            PageHeader { title: qsTr("SMS-Überwachung") }

            // change warning (SIM swap / firmware flash detected vs baseline)
            Rectangle {
                visible: monitor.simChanged || monitor.firmwareChanged
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: warn.height + 2 * Theme.paddingMedium
                radius: Theme.paddingSmall
                color: Qt.rgba(1, 0.33, 0.33, 0.18)
                border.color: "#ff5555"; border.width: 1
                Label {
                    id: warn
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.paddingMedium
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: "#ff5555"
                    text: (monitor.simChanged ? qsTr("⚠ SIM hat sich seit dem letzten Start geändert! ") : "")
                          + (monitor.firmwareChanged ? qsTr("⚠ Modem-Firmware hat sich geändert! ") : "")
                          + qsTr("(im Pulley quittieren)")
                }
            }

            DetailItem { visible: page.showSim; label: qsTr("Modem-Firmware"); value: monitor.firmwareVersion || "–" }
            DetailItem { visible: page.showSim; label: qsTr("IMEI"); value: monitor.imei || "–" }
            DetailItem { visible: page.showSim; label: qsTr("SIM ICCID"); value: monitor.simIccid || "–" }
            DetailItem { visible: page.showSim; label: qsTr("SIM IMSI"); value: monitor.simImsi || "–" }
            DetailItem { visible: page.showSim; label: qsTr("SIM-Betreiber"); value: monitor.simOperator || "–" }
            BackgroundItem {
                visible: !page.showSim
                width: parent.width
                height: Theme.itemSizeSmall
                onClicked: page.showSim = true
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("SIM-/Geräte-Kennungen verborgen — antippen zum Anzeigen")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: monitor.tier2Active ? Theme.highlightColor : Theme.secondaryColor
                text: monitor.tier2Active
                      ? qsTr("Tier 2 aktiv — Baseband wird überwacht.")
                      : qsTr("Tier 2 nicht aktiv. Der /dev/diag-Collector ist noch nicht " +
                             "verbunden; hier erscheinen dann stille, Binär-, OTA-, Flash- " +
                             "und andere auffällige SMS.")
            }
        }

        PullDownMenu {
            MenuItem {
                text: monitor.tier2Active ? qsTr("Tier 2 (Baseband) stoppen")
                                          : qsTr("Tier 2 (Baseband) starten — Root")
                onClicked: monitor.tier2Active ? monitor.stopTier2()
                                               : monitor.startTier2()
            }
            MenuItem {
                visible: monitor.simChanged || monitor.firmwareChanged
                text: qsTr("Identitäts-Warnung quittieren")
                onClicked: monitor.acknowledgeIdentity()
            }
            MenuItem {
                text: qsTr("Als CSV exportieren")
                enabled: monitor.smsEvents.length > 0
                onClicked: {
                    var p = monitor.exportSmsCsv()
                    if (p.length > 0) list.showExportHint(p)
                }
            }
            MenuItem {
                text: qsTr("Liste leeren")
                enabled: monitor.smsEvents.length > 0
                onClicked: monitor.clearSmsEvents()
            }
        }

        function showExportHint(p) { exportHint.text = qsTr("Exportiert: ") + p; exportHint.visible = true }

        delegate: ListItem {
            id: item
            contentHeight: col.height + Theme.paddingMedium
            onClicked: pageStack.push(Qt.resolvedUrl("SMSDetailPage.qml"),
                                      { event: modelData })
            onPressAndHold: openMenu()
            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Löschen")
                    onClicked: {
                        // capture the STABLE id by value; the page-level popup
                        // outlives this delegate (the model rebuilds on every
                        // delete, so a per-delegate RemorseItem would be orphaned
                        // and freeze the list when deleting several in a row).
                        var id = modelData._id
                        smsRemorse.execute(qsTr("Gelöscht"), function() {
                            monitor.removeSmsEventById(id)
                        })
                    }
                }
            }
            Column {
                id: col
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                y: Theme.paddingSmall
                Row {
                    width: parent.width
                    spacing: Theme.paddingSmall
                    Rectangle {
                        width: Theme.paddingSmall; height: width; radius: width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        color: page.sevColor(modelData.severity)
                    }
                    Label {
                        text: modelData.type ? modelData.type : qsTr("SMS")
                        font.pixelSize: Theme.fontSizeSmall
                        color: page.sevColor(modelData.severity)
                    }
                    Item { width: Theme.paddingMedium; height: 1 }
                    Label {
                        text: page.sevLabel(modelData.severity)
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: parent.width - x
                        horizontalAlignment: Text.AlignRight
                        text: modelData.ts ? modelData.ts.slice(11, 19) : ""
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: [
                        modelData.from ? qsTr("Von: ") + modelData.from : "",
                        modelData.smsc ? qsTr("SMSC: ") + modelData.smsc : "",
                        modelData.pid ? "PID " + modelData.pid : "",
                        modelData.dcs ? "DCS " + modelData.dcs : ""
                    ].filter(function(s){ return s.length }).join("  ·  ")
                }
            }
        }

        ViewPlaceholder {
            enabled: monitor.smsEvents.length === 0
            text: qsTr("Keine auffälligen SMS")
            hintText: monitor.tier2Active ? qsTr("Es wird überwacht …")
                                          : qsTr("Wisch nach rechts: Erklärungen")
        }

        VerticalScrollDecorator {}
    }

    Label {
        id: exportHint
        visible: false
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right
                  margins: Theme.paddingMedium }
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.highlightColor
        Timer { running: exportHint.visible; interval: 6000; onTriggered: exportHint.visible = false }
    }

    // Page-level remorse for row deletion. Kept OUTSIDE the list so it survives
    // the model rebuild that each delete triggers — a per-delegate RemorseItem
    // would be destroyed mid-timer and freeze the list when deleting several.
    RemorsePopup { id: smsRemorse }
}
