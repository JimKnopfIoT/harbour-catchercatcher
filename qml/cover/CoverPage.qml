import QtQuick 2.0
import Sailfish.Silica 1.0
import catchercatcher 1.0

CoverBackground {
    id: cover
    property CellMonitor monitor

    function threatColor(l) {
        if (l >= 3) return "#ff5555"
        if (l === 2) return "#ffaa00"
        if (l === 1) return Theme.highlightColor
        return Theme.primaryColor
    }
    function threatLabel(l) {
        if (l >= 3) return "ALARM"
        if (l === 2) return "Warnung"
        if (l === 1) return "Hinweis"
        return ""
    }

    // --- faint oversized app-logo watermark, tinted in the ambience colour
    // (like Fernschreiber's cover). Drawn from the icon motif — signal waves
    // radiating from a cell mast inside the detector's target ring — so it
    // scales crisply and needs no bitmap. Qt 5.6 Canvas has no setLineDash(),
    // so the ring is drawn as dash segments by hand.
    Canvas {
        id: watermark
        anchors.fill: parent
        opacity: 0.16
        // the watermark itself carries the threat state: ambient tint when clear,
        // amber/red on a warning/alarm. Repaints whenever that colour changes.
        property color tint: threatColor(monitor.threatLevel)
        onTintChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            // motif is authored in a 172x172 box centred on the emitter (86,83);
            // scale it up so it bleeds off the cover edges.
            var S = Math.max(width, height) * 1.7 / 172
            ctx.translate(width / 2 - 86 * S, height / 2 - 83 * S)
            ctx.scale(S, S)
            ctx.strokeStyle = tint
            ctx.fillStyle = tint
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            // signal waves — concentric arcs radiating left & right from the mast
            ctx.lineWidth = 7
            var cx = 86, cy = 83
            var radii = [26, 46]
            for (var i = 0; i < radii.length; i++) {
                var r = radii[i]
                ctx.beginPath(); ctx.arc(cx, cy, r, -0.9, 0.9); ctx.stroke()          // right
                ctx.beginPath(); ctx.arc(cx, cy, r, Math.PI - 0.9, Math.PI + 0.9); ctx.stroke() // left
            }

            // cell mast + antenna
            ctx.lineWidth = 8
            ctx.beginPath()
            ctx.moveTo(86, 66); ctx.lineTo(68, 128)
            ctx.moveTo(86, 66); ctx.lineTo(104, 128)
            ctx.moveTo(74, 104); ctx.lineTo(98, 104)
            ctx.stroke()
            ctx.beginPath(); ctx.arc(86, 60, 10, 0, 2 * Math.PI); ctx.fill()

            // dashed detector target ring (hand-drawn dashes)
            ctx.lineWidth = 6
            var rr = 52, segs = 20
            for (var s = 0; s < segs; s += 2) {
                ctx.beginPath()
                ctx.arc(cx, cy, rr, (s / segs) * 2 * Math.PI,
                        ((s + 1) / segs) * 2 * Math.PI)
                ctx.stroke()
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium

        // alarm text only when there is actually a threat (otherwise clean)
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: monitor.threatLevel > 0
            text: threatLabel(monitor.threatLevel)
            color: threatColor(monitor.threatLevel)
            font.pixelSize: Theme.fontSizeLarge
            font.bold: true
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: monitor.technology ? monitor.technology.toUpperCase() : "–"
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: monitor.monitoring ? "●" : "❙❙"
            color: monitor.monitoring ? Theme.highlightColor : Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
    }

    CoverActionList {
        CoverAction {
            iconSource: monitor.monitoring ? "image://theme/icon-cover-pause"
                                           : "image://theme/icon-cover-play"
            onTriggered: monitor.monitoring ? monitor.stop() : monitor.start()
        }
    }
}
