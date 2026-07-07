import QtQuick 2.0
import Sailfish.Silica 1.0
import catchercatcher 1.0

Page {
    id: page
    property CellMonitor monitor

    allowedOrientations: Orientation.All

    function threatColor(l) {
        if (l >= 3) return "#ff5555"
        if (l === 2) return "#ffaa00"
        if (l === 1) return Theme.highlightColor
        return Theme.secondaryHighlightColor
    }
    function threatLabel(l) {
        if (l >= 3) return qsTr("ALARM")
        if (l === 2) return qsTr("Warnung")
        if (l === 1) return qsTr("Hinweis")
        return qsTr("Unauffällig")
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: qsTr("Ereignisprotokoll")
                onClicked: pageStack.push(Qt.resolvedUrl("EventLogPage.qml"),
                                          { monitor: page.monitor })
            }
            MenuItem {
                text: monitor.logging ? qsTr("Logging stoppen")
                                      : qsTr("Logging in Datei starten")
                onClicked: monitor.setLogging(!monitor.logging)
            }
            MenuItem {
                visible: monitor.threatLevel > 0
                text: qsTr("Alarm quittieren")
                onClicked: monitor.acknowledge()
            }
            MenuItem {
                text: monitor.monitoring ? qsTr("Überwachung stoppen")
                                         : qsTr("Überwachung starten")
                onClicked: monitor.monitoring ? monitor.stop() : monitor.start()
            }
        }

        // swipe up → info / about page. (SMS is now to the left: swipe right;
        // the map is to the right: swipe left.)
        PushUpMenu {
            MenuItem {
                text: qsTr("Info / Grenzen ↑")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }

        Column {
            id: column
            width: page.width
            spacing: Theme.paddingSmall

            PageHeader { title: qsTr("Catcher Catcher") }

            // --- threat banner ------------------------------------------
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: threatCol.height + 2 * Theme.paddingMedium
                radius: Theme.paddingMedium
                color: Qt.rgba(0, 0, 0, 0.15)
                border.width: 2
                border.color: threatColor(monitor.threatLevel)

                Column {
                    id: threatCol
                    y: Theme.paddingMedium
                    width: parent.width
                    spacing: Theme.paddingSmall
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: threatLabel(monitor.threatLevel)
                        font.pixelSize: Theme.fontSizeHuge
                        color: threatColor(monitor.threatLevel)
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 2 * Theme.paddingLarge
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: monitor.threatText.length > 0
                        text: monitor.threatText
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: monitor.monitoring ? qsTr("überwacht …")
                                                 : qsTr("angehalten")
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                    // reset an active alarm right here (also in the pulley)
                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: monitor.threatLevel > 0
                        text: qsTr("Zurücksetzen")
                        onClicked: monitor.acknowledge()
                    }
                }
            }

            // --- serving cell details (compact 2-column grid) -----------
            Grid {
                id: cellGrid
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                columns: 2
                columnSpacing: Theme.paddingMedium
                rowSpacing: Theme.paddingSmall
                property real cellW: (width - columnSpacing) / 2

                CellField { width: cellGrid.cellW; label: qsTr("Betreiber");  value: monitor.operatorName || "–" }
                CellField { width: cellGrid.cellW; label: qsTr("Technik");    value: monitor.technology ? monitor.technology.toUpperCase() : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("MCC / MNC");  value: monitor.mcc && monitor.mnc ? monitor.mcc + " / " + monitor.mnc : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("Cell ID");    value: monitor.cellId ? String(monitor.cellId) : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("eNodeB");     value: monitor.enbId >= 0 ? String(monitor.enbId) : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("LAC / TAC");  value: monitor.lac >= 0 ? String(monitor.lac) : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("EARFCN");     value: monitor.earfcn >= 0 ? String(monitor.earfcn) : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("Signal");     value: monitor.strength >= 0 ? monitor.strength + " %" : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("RSRP");       value: monitor.rsrp >= 0 ? String(monitor.rsrp) : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("RSRQ");       value: monitor.rsrq >= 0 ? String(monitor.rsrq) : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("Nachbarzellen"); value: monitor.neighbourCount >= 0 ? String(monitor.neighbourCount) : "–" }
                CellField { width: cellGrid.cellW; label: qsTr("Präferenz");  value: monitor.technologyPreference ? monitor.technologyPreference.toUpperCase() : "–" }
            }

            // whitelist — compact tappable row (long-press opens the editor too)
            BackgroundItem {
                width: parent.width
                height: wlField.height + Theme.paddingSmall
                function openWhitelist() {
                    pageStack.push(Qt.resolvedUrl("WhitelistPage.qml"),
                                   { monitor: page.monitor })
                }
                onClicked: openWhitelist()
                onPressAndHold: openWhitelist()
                CellField {
                    id: wlField
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    label: qsTr("Whitelist")
                    value: (monitor.currentCellTrusted ? qsTr("vertrauenswürdig") : qsTr("unbekannt"))
                           + "  (" + monitor.whitelist.length + ") ›"
                }
            }

            SectionHeader { text: qsTr("Schutz & Whitelist") }

            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingMedium

                Button {
                    text: monitor.currentCellTrusted ? qsTr("Zelle misstrauen")
                                                     : qsTr("Zelle vertrauen")
                    onClicked: monitor.currentCellTrusted ? monitor.untrustCurrentCell()
                                                          : monitor.trustCurrentCell()
                }
                Button {
                    text: monitor.technologyPreference === "lte" ? qsTr("Alle Netze")
                                                                 : qsTr("Nur LTE (2G sperren)")
                    onClicked: monitor.technologyPreference === "lte" ? monitor.allowAllRat()
                                                                      : monitor.lockLteOnly()
                }
                Button {
                    text: qsTr("Modem trennen")
                    onClicked: monitor.goOffline()
                }
            }

            TextSwitch {
                text: qsTr("Auto-Schutz")
                description: qsTr("Bei Alarm automatisch auf Nur-LTE umschalten")
                checked: monitor.autoProtect
                automaticCheck: false
                onClicked: monitor.setAutoProtect(!monitor.autoProtect)
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                visible: !monitor.available
                text: qsTr("Keine Verbindung zu ofono (/ril_0). Ist eine SIM eingelegt?")
                color: "#ff5555"
                font.pixelSize: Theme.fontSizeSmall
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
    }

    property bool _mapAttached: false

    Component.onCompleted: if (!monitor.monitoring) monitor.start()

    // Attach the map to the right (swipe left) once the page is actually active
    // (doing this in Component.onCompleted is too early — the page isn't on the
    // stack yet, so the swipe-left attached page silently never appears).
    onStatusChanged: {
        if (status === PageStatus.Active && !_mapAttached) {
            _mapAttached = true
            pageStack.pushAttached(Qt.resolvedUrl("MapPage.qml"),
                                   { monitor: page.monitor })
        }
    }
}
