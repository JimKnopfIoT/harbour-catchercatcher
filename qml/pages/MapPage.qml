import QtQuick 2.0
import Sailfish.Silica 1.0
import catchercatcher 1.0

/*
 * MapPage — plots the signal samples of the current cell over an OpenStreetMap
 * silhouette (streets + buildings, fetched via the reused `osm` helper) and
 * marks the signal-weighted best guess of the transmitter.
 *
 * Localisation caveat: a single cell can't be triangulated from one spot. Walk
 * or drive around while monitoring; the estimate sharpens as samples pile up
 * and biases toward where the signal is strongest (nearest the transmitter).
 */
Page {
    id: page
    property CellMonitor monitor

    // radius (m) shown at zoom 1.0 — the map opens this close, not zoomed out
    property real startRadiusM: 200

    // toggleable overlay layers. Initial values come from the persisted prefs on
    // the monitor (restored in Component.onCompleted) so they survive both a page
    // switch and an app restart.
    property bool mapEnabled: false     // OSM streets
    property bool showTowers: false     // OpenCellID known towers
    property bool showLicensed: false   // BNetzA licensed sites (dashed circles)
    property bool showSamples: true     // our signal samples
    property bool showEstimate: true    // transmitter estimate
    property bool rotateWithCompass: false   // map rotates heading-up (gyro, like iBT)
    // relative heading in radians, applied inside the projection (0 when off)
    property real headingRad: (rotateWithCompass && sensor.active)
                              ? sensor.heading * Math.PI / 180 : 0
    allowedOrientations: Orientation.All

    Component.onDestruction: sensor.stop()

    // attach the general glossary as the rightmost page (swipe left from the map)
    property bool _glossaryAttached: false
    onStatusChanged: {
        if (status === PageStatus.Active && !_glossaryAttached) {
            _glossaryAttached = true
            pageStack.pushAttached(Qt.resolvedUrl("GlossaryPage.qml"),
                                   { monitor: page.monitor })
        }
    }

    // restore the layer toggles the user last had on (persisted on the monitor),
    // so switching to SMS and back — or restarting the app — keeps the map set up
    // exactly as it was, without re-toggling GPS/Karte every time.
    Component.onCompleted: {
        mapEnabled = monitor.mapStreets
        showTowers = monitor.mapTowers
        showLicensed = monitor.mapLicensed
        rotateWithCompass = monitor.mapRotate
        if (mapEnabled) { fetchOsm(); fetchWide(); fetchFar() }
        if (showTowers && ocid.hasKey) fetchTowers()
        if (showLicensed) fetchLicensed(false)
        if (rotateWithCompass && !sensor.active) sensor.start()
    }

    // bounding box (deg) of the current view centre, for tower fetches
    function centreLat() { return monitor.lat }
    function centreLon() { return monitor.lon }
    function fetchTowers() {
        if (!showTowers || !monitor.gpsValid || ocid.busy || !ocid.hasKey)
            return
        var dLat = 0.009, dLon = 0.013   // ~1 km half-box (OCID caps at 2x2 km)
        ocid.fetchArea(centreLat() - dLat, centreLon() - dLon,
                       centreLat() + dLat, centreLon() + dLon)
    }

    // BNetzA licensed sites: the box follows the view (grows as you zoom out),
    // capped at ~5 km. Cached & deduped by fID, so re-fetching an already-loaded
    // area is a no-op (areaCached() short-circuits in C++). force=true reloads.
    function fetchLicensed(force) {
        if (!monitor.gpsValid || bnetza.busy) return
        var r = Math.min(page.desiredRadius(), 5000)
        if (r < 700) r = 700
        var dLat = r / 111320
        var dLon = r / (111320 * Math.cos(centreLat() * Math.PI / 180))
        bnetza.fetchArea(centreLat() - dLat, centreLon() - dLon,
                         centreLat() + dLat, centreLon() + dLon, force === true)
    }
    function maybeRefetchLicensed() {
        // cheap: fetchLicensed(false) hits the cache unless the (grown) box
        // reaches tiles we haven't loaded yet — then it fills them in.
        if (page.showLicensed && monitor.gpsValid && !bnetza.busy)
            fetchLicensed(false)
    }

    property real fetchedWide: 0
    property real fetchedFar: 0

    // radius that covers the *visible* extent (grows as you zoom out)
    function desiredRadius() {
        var mpp = canvas.metersPerPx
        if (mpp <= 0) return 500
        var r = mpp * Math.max(mapBox.width, mapBox.height) / 2 * 1.2
        // hard ceiling: never load map data beyond 10 km.
        return Math.max(300, Math.min(10000, r))
    }

    // near ring: streets + buildings, fixed 500 m around you (mode 2, heaviest)
    function fetchOsm() {
        if (mapEnabled && monitor.gpsValid && !osm.busy)
            osm.fetch(monitor.lat, monitor.lon, 500, 2)
    }
    function maybeRefetch() {
        if (!mapEnabled || !monitor.gpsValid || osm.busy) return
        if (osm.ways.length === 0) { fetchOsm(); return }
        var dN = (monitor.lat - osm.lat) * 111320
        var dE = (monitor.lon - osm.lon) * 111320 * Math.cos(monitor.lat * Math.PI / 180)
        if (Math.sqrt(dN * dN + dE * dE) > 250) fetchOsm()
    }

    // mid ring: streets + land plots, no buildings, fixed 1 km (mode 1)
    function fetchWide() {
        if (!mapEnabled || !monitor.gpsValid || osmWide.busy) return
        osmWide.fetch(monitor.lat, monitor.lon, 1000, 1)
    }
    function maybeRefetchWide() {
        if (!mapEnabled || !monitor.gpsValid || osmWide.busy) return
        if (osmWide.ways.length === 0) { fetchWide(); return }
        var dN = (monitor.lat - osmWide.lat) * 111320
        var dE = (monitor.lon - osmWide.lon) * 111320 * Math.cos(monitor.lat * Math.PI / 180)
        if (Math.sqrt(dN * dN + dE * dE) > 500) fetchWide()
    }

    // far ring: only coarse/MAJOR roads (mode 0, lightest), zoom-scaled — only
    // past the 1 km mid ring, capped at 10 km. No plots/houses/minor streets.
    function fetchFar() {
        if (!mapEnabled || !monitor.gpsValid || osmFar.busy) return
        var r = page.desiredRadius()
        if (r < 1000) return
        page.fetchedFar = r
        osmFar.fetch(monitor.lat, monitor.lon, r, 0)
    }
    function maybeRefetchFar() {
        if (!mapEnabled || !monitor.gpsValid || osmFar.busy) return
        if (page.desiredRadius() < 1000) return
        if (osmFar.ways.length === 0) { fetchFar(); return }
        var dN = (monitor.lat - osmFar.lat) * 111320
        var dE = (monitor.lon - osmFar.lon) * 111320 * Math.cos(monitor.lat * Math.PI / 180)
        if (Math.sqrt(dN * dN + dE * dE) > page.fetchedFar * 0.5
                || page.desiredRadius() > page.fetchedFar * 1.3)
            fetchFar()
    }

    Connections {
        target: monitor
        onGpsChanged: {
            if (monitor.gpsValid) {
                page.fetchOsm(); page.fetchWide(); page.fetchFar()
                page.fetchTowers(); page.maybeRefetchLicensed()
            }
        }
        onPosChanged: {
            page.maybeRefetch(); page.maybeRefetchWide(); page.maybeRefetchFar()
            page.maybeRefetchTowers(); page.maybeRefetchLicensed()
        }
    }

    function maybeRefetchTowers() {
        if (!showTowers || !monitor.gpsValid || ocid.busy || !ocid.hasKey)
            return
        if (ocid.cells.length === 0) { fetchTowers(); return }
        var dN = (centreLat() - ocid.lat) * 111320
        var dE = (centreLon() - ocid.lon) * 111320 * Math.cos(centreLat() * Math.PI / 180)
        if (Math.sqrt(dN * dN + dE * dE) > 500)
            fetchTowers()
    }

    // ---- geographic bounds: always a sane box around the current centre ----
    function bounds() {
        var haveCentre = monitor.gpsValid || monitor.estValid
        // box is centred on YOU (GPS); fall back to the estimate only if no fix
        var cLa = monitor.gpsValid ? monitor.lat : monitor.estLat
        var cLo = monitor.gpsValid ? monitor.lon : monitor.estLon
        if (!haveCentre) return null

        // minimum half-span ~500 m so streets + user marker are visible even
        // when standing still.
        var la0 = cLa - 0.0050, la1 = cLa + 0.0050
        var lo0 = cLo - 0.0075, lo1 = cLo + 0.0075
        function acc(la, lo) {
            if (la === 0 && lo === 0) return
            la0 = Math.min(la0, la); la1 = Math.max(la1, la)
            lo0 = Math.min(lo0, lo); lo1 = Math.max(lo1, lo)
        }
        var s = monitor.samples
        for (var i = 0; i < s.length; i++) acc(s[i].lat, s[i].lon)
        // NOTE: the sender estimate is deliberately NOT folded into the box — it
        // can be a BNetzA mast km away (snapped), and must not pull the view or the
        // rotation centre off you. It's drawn as a marker only.
        return { la0: la0, la1: la1, lo0: lo0, lo1: lo1 }
    }

    function sigColor(sig, lo, hi) {
        var t = hi > lo ? (sig - lo) / (hi - lo) : 0.5
        t = Math.max(0, Math.min(1, t))
        return Qt.rgba(Math.round(255 * t) / 255, 0.2, Math.round(255 * (1 - t)) / 255, 0.9)
    }

    // round a distance up to a "nice" value for the scale bar
    function niceDist(m) {
        var steps = [10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000]
        for (var i = 0; i < steps.length; i++)
            if (steps[i] >= m) return steps[i]
        return steps[steps.length - 1]
    }
    function fmtDist(m) {
        return m >= 1000 ? (m / 1000) + " km" : m + " m"
    }

    SilicaFlickable {
        anchors.fill: parent

        PullDownMenu {
            MenuItem {
                text: monitor.gpsEnabled ? qsTr("GPS ausschalten") : qsTr("GPS einschalten")
                onClicked: monitor.gpsEnabled = !monitor.gpsEnabled
            }
            MenuItem {
                text: ocid.hasKey ? qsTr("OpenCellID-Key ändern…")
                                  : qsTr("OpenCellID-Key eingeben…")
                onClicked: pageStack.push(Qt.resolvedUrl("OcidKeyDialog.qml"))
            }
            MenuItem {
                text: qsTr("BNetzA-Standorte laden")
                onClicked: { page.showLicensed = true; monitor.mapLicensed = true
                             page.fetchLicensed(true); canvas.requestPaint() }
            }
            MenuItem {
                text: qsTr("Samples dieser Zelle löschen")
                onClicked: monitor.clearSamples()
            }
        }

        Column {
            id: col
            width: parent.width

            PageHeader { title: qsTr("Ortung") }

            // --- GPS coordinate readout ---
            DetailItem {
                label: qsTr("GPS")
                value: !monitor.gpsEnabled ? qsTr("aus")
                       : (monitor.gpsValid ? qsTr("Fix vorhanden") : qsTr("suche Fix…"))
            }
            DetailItem {
                label: qsTr("Breite")
                value: monitor.gpsValid ? monitor.lat.toFixed(6) + "°" : "–"
            }
            DetailItem {
                label: qsTr("Länge")
                value: monitor.gpsValid ? monitor.lon.toFixed(6) + "°" : "–"
            }
            DetailItem {
                label: qsTr("Zelle / Samples")
                value: monitor.cellId + " / " + monitor.samples.length
            }
            DetailItem {
                label: qsTr("Schätzung Sender")
                value: monitor.estValid
                       ? monitor.estLat.toFixed(5) + ", " + monitor.estLon.toFixed(5)
                         + (monitor.estSuspect ? " " + qsTr("(verdächtig – nicht gesnappt)")
                            : monitor.estOfficial ? " " + qsTr("(amtl. BNetzA-Standort)") : "")
                       : qsTr("noch zu wenig Bewegung")
            }
            // --- overlay layer toggles ---
            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall

                LayerChip {
                    text: qsTr("Karte")
                    active: page.mapEnabled
                    onToggled: {
                        page.mapEnabled = !page.mapEnabled
                        monitor.mapStreets = page.mapEnabled
                        if (page.mapEnabled) { page.fetchOsm(); page.fetchWide() }
                        canvas.requestPaint()
                    }
                }
                LayerChip {
                    text: qsTr("OCID")
                    active: page.showTowers
                    activeColor: "#ffb300"
                    onToggled: {
                        page.showTowers = !page.showTowers
                        monitor.mapTowers = page.showTowers
                        if (page.showTowers) {
                            if (!ocid.hasKey) pageStack.push(Qt.resolvedUrl("OcidKeyDialog.qml"))
                            else page.fetchTowers()
                        }
                        canvas.requestPaint()
                    }
                }
                LayerChip {
                    text: qsTr("BNetzA")
                    active: page.showLicensed
                    activeColor: "#7ec8ff"
                    onToggled: {
                        page.showLicensed = !page.showLicensed
                        monitor.mapLicensed = page.showLicensed
                        // convenience: enabling with an empty cache loads once;
                        // the pulley entry is the explicit "reload this area".
                        if (page.showLicensed && bnetza.sites.length === 0)
                            page.fetchLicensed(false)
                        canvas.requestPaint()
                    }
                }
                LayerChip {
                    text: qsTr("Samples")
                    active: page.showSamples
                    onToggled: { page.showSamples = !page.showSamples; canvas.requestPaint() }
                }
                LayerChip {
                    text: qsTr("Schätzung")
                    activeColor: "#ffb300"
                    active: page.showEstimate
                    onToggled: { page.showEstimate = !page.showEstimate; canvas.requestPaint() }
                }
                LayerChip {
                    text: qsTr("Kompass")
                    active: page.rotateWithCompass
                    onToggled: {
                        page.rotateWithCompass = !page.rotateWithCompass
                        // Start the sensor once and keep it running; toggling only
                        // applies/removes the rotation. Restarting it (as before)
                        // re-snapped the heading to a not-yet-settled QCompass
                        // reading whose frame could be 90° off.
                        if (page.rotateWithCompass && !sensor.active)
                            sensor.start()
                        canvas.requestPaint()
                    }
                }
            }

            Label {
                visible: (page.mapEnabled && osm.busy) || (page.showTowers && ocid.busy)
                         || (page.showTowers && ocid.status.length > 0)
                         || (page.showLicensed && bnetza.status.length > 0)
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                text: {
                    var parts = []
                    if (page.mapEnabled && osm.busy) parts.push(qsTr("lade Karte…"))
                    if (page.showTowers && (ocid.busy || ocid.status.length > 0)) parts.push(ocid.status)
                    if (page.showLicensed && bnetza.status.length > 0) parts.push(bnetza.status)
                    return parts.join("   •   ")
                }
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }


            Item {
                id: mapBox
                width: parent.width
                height: page.height - y - Theme.paddingLarge
                clip: true

                function resetView() { canvas.zoom = 1.0; canvas.panX = 0; canvas.panY = 0; canvas.requestPaint() }

                // static background so the rotating map layer looks clean
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(0, 0, 0, 0.15)
                }

                PinchArea {
                    anchors.fill: parent
                    anchors.margins: Theme.paddingMedium
                    onPinchUpdated: {
                        var f = 1 + (pinch.scale - pinch.previousScale)
                        canvas.zoom = Math.max(0.02, Math.min(40, canvas.zoom * f))
                        canvas.requestPaint()
                    }

                    Canvas {
                        id: canvas
                        anchors.fill: parent

                        property real zoom: 1.0
                        property real panX: 0
                        property real panY: 0
                        property real metersPerPx: 0   // for the scale bar

                        // zooming out grows the far (streets) ring + BNetzA box
                        onZoomChanged: { page.maybeRefetchFar(); page.maybeRefetchLicensed() }

                        Connections {
                            target: monitor
                            onSamplesChanged: canvas.requestPaint()
                            onEstChanged: canvas.requestPaint()
                            onPosChanged: canvas.requestPaint()
                            onGpsChanged: canvas.requestPaint()
                        }
                        Connections {
                            target: osm
                            onChanged: canvas.requestPaint()
                        }
                        Connections {
                            target: osmWide
                            onChanged: canvas.requestPaint()
                        }
                        Connections {
                            target: osmFar
                            onChanged: canvas.requestPaint()
                        }
                        Connections {
                            target: ocid
                            onChanged: canvas.requestPaint()
                        }
                        Connections {
                            target: bnetza
                            onChanged: canvas.requestPaint()
                        }
                        // repaint while rotating so the heading-up view follows
                        Timer {
                            interval: 150
                            repeat: true
                            running: page.rotateWithCompass
                            onTriggered: canvas.requestPaint()
                        }

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()

                        var b = page.bounds()
                        if (!b) return

                        var cosLat = Math.cos((b.la0 + b.la1) / 2 * Math.PI / 180)
                        // Fixed default view: page.startRadiusM metres radius at
                        // zoom 1.0, independent of how far the GPS-sample track
                        // spreads. So the map always (re)opens close-in — not
                        // zoomed out to the whole track — and pinch (0.02..40)
                        // covers ~10 km out to a few m in.
                        var refSpanDeg = (2 * page.startRadiusM) / 111320
                        var baseScale = Math.min(width, height) / refSpanDeg
                        var eff = baseScale * canvas.zoom
                        canvas.metersPerPx = 111320 / eff   // 1° lat ≈ 111320 m
                        // rotate/centre on YOU (GPS), never on the snapped estimate
                        var bcLo = monitor.gpsValid ? monitor.lon : (b.lo0 + b.lo1) / 2
                        var bcLa = monitor.gpsValid ? monitor.lat : (b.la0 + b.la1) / 2
                        // heading-up projection (rotate the ENU vector by the
                        // relative heading), north-up when the compass is off.
                        var hh = page.headingRad
                        var ch = Math.cos(hh), sh = Math.sin(hh)
                        function px(la, lo) {
                            var north = (la - bcLa)
                            var east = (lo - bcLo) * cosLat
                            var e2 = east * ch - north * sh
                            var n2 = east * sh + north * ch
                            return {
                                x: width / 2 + canvas.panX + e2 * eff,
                                y: height / 2 + canvas.panY - n2 * eff
                            }
                        }

                        if (page.mapEnabled) {
                            // far ring: streets only, faintest, underneath everything
                            var fways = osmFar.ways
                            for (var fw = 0; fw < fways.length; fw++) {
                                var fpts = fways[fw].pts
                                if (!fpts || fpts.length < 2) continue
                                ctx.beginPath()
                                for (var fk = 0; fk < fpts.length; fk++) {
                                    var fq = px(fpts[fk].lat, fpts[fk].lon)
                                    if (fk === 0) ctx.moveTo(fq.x, fq.y); else ctx.lineTo(fq.x, fq.y)
                                }
                                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.12)
                                ctx.lineWidth = 1
                                ctx.stroke()
                            }

                            // mid ring: streets + plots, faint
                            var wways = osmWide.ways
                            for (var ww = 0; ww < wways.length; ww++) {
                                var wpts = wways[ww].pts
                                if (!wpts || wpts.length < 2) continue
                                ctx.beginPath()
                                for (var wk = 0; wk < wpts.length; wk++) {
                                    var wq = px(wpts[wk].lat, wpts[wk].lon)
                                    if (wk === 0) ctx.moveTo(wq.x, wq.y); else ctx.lineTo(wq.x, wq.y)
                                }
                                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.15)
                                ctx.lineWidth = 1
                                ctx.stroke()
                            }

                            var ways = osm.ways
                            for (var w = 0; w < ways.length; w++) {
                                var pts = ways[w].pts
                                if (!pts || pts.length < 2) continue
                                ctx.beginPath()
                                for (var k = 0; k < pts.length; k++) {
                                    var q = px(pts[k].lat, pts[k].lon)
                                    if (k === 0) ctx.moveTo(q.x, q.y); else ctx.lineTo(q.x, q.y)
                                }
                                ctx.strokeStyle = ways[w].building
                                        ? Qt.rgba(1, 1, 1, 0.20) : Qt.rgba(1, 1, 1, 0.34)
                                ctx.lineWidth = 1
                                ctx.stroke()
                            }
                        }

                        // BNetzA licensed sites — dashed uncertainty circles
                        // (coordinates are privacy-rounded, so ~75 m ≈ "here-ish").
                        // Dashes drawn as arc segments by hand: Qt 5.6's Canvas
                        // Context2D has no setLineDash(), and calling it throws and
                        // aborts the whole onPaint.
                        if (page.showLicensed) {
                            var bs = bnetza.sites
                            var rPx = canvas.metersPerPx > 0 ? 75 / canvas.metersPerPx : 6
                            if (rPx < 4) rPx = 4
                            ctx.strokeStyle = "#7ec8ff"
                            ctx.lineWidth = 2
                            var segs = 16   // 8 dashes (every other segment)
                            for (var li = 0; li < bs.length; li++) {
                                var bla = bs[li].lat, blo = bs[li].lon
                                // cheap cull: skip sites well outside the view box
                                if (bla < b.la0 - 0.02 || bla > b.la1 + 0.02
                                        || blo < b.lo0 - 0.02 || blo > b.lo1 + 0.02) continue
                                var bp = px(bla, blo)
                                for (var sg = 0; sg < segs; sg += 2) {
                                    ctx.beginPath()
                                    ctx.arc(bp.x, bp.y, rPx,
                                            (sg / segs) * 2 * Math.PI,
                                            ((sg + 1) / segs) * 2 * Math.PI)
                                    ctx.stroke()
                                }
                            }
                        }

                        // known towers (OpenCellID) — green triangles
                        if (page.showTowers) {
                            var tc = ocid.cells
                            var th = Theme.paddingMedium / 2
                            ctx.fillStyle = "#5fd08a"
                            for (var t = 0; t < tc.length; t++) {
                                var tp = px(tc[t].lat, tc[t].lon)
                                ctx.beginPath()
                                ctx.moveTo(tp.x, tp.y - th)
                                ctx.lineTo(tp.x - th, tp.y + th)
                                ctx.lineTo(tp.x + th, tp.y + th)
                                ctx.closePath()
                                ctx.fill()
                            }
                        }

                        if (page.showSamples) {
                            var s = monitor.samples
                            var lo = 1e9, hi = -1e9
                            for (var i = 0; i < s.length; i++) { lo = Math.min(lo, s[i].sig); hi = Math.max(hi, s[i].sig) }
                            for (i = 0; i < s.length; i++) {
                                var p = px(s[i].lat, s[i].lon)
                                ctx.beginPath()
                                ctx.arc(p.x, p.y, Theme.paddingSmall / 2, 0, 2 * Math.PI)
                                ctx.fillStyle = page.sigColor(s[i].sig, lo, hi)
                                ctx.fill()
                            }
                        }

                        if (monitor.gpsValid) {
                            var u = px(monitor.lat, monitor.lon)
                            ctx.beginPath()
                            ctx.arc(u.x, u.y, Theme.paddingMedium / 2, 0, 2 * Math.PI)
                            ctx.strokeStyle = Theme.highlightColor
                            ctx.lineWidth = 3
                            ctx.stroke()
                        }

                        if (page.showEstimate && monitor.estValid) {
                            var e = px(monitor.estLat, monitor.estLon)
                            // red when the estimate must not be read as a benign
                            // tower (threat or ghost-flagged, never snapped), else amber
                            ctx.strokeStyle = monitor.estSuspect ? "#ff3b30" : "#ffb300"
                            ctx.lineWidth = 3
                            ctx.beginPath(); ctx.arc(e.x, e.y, Theme.itemSizeSmall / 2, 0, 2 * Math.PI); ctx.stroke()
                            ctx.beginPath()
                            ctx.moveTo(e.x - Theme.paddingLarge, e.y); ctx.lineTo(e.x + Theme.paddingLarge, e.y)
                            ctx.moveTo(e.x, e.y - Theme.paddingLarge); ctx.lineTo(e.x, e.y + Theme.paddingLarge)
                            ctx.stroke()
                        }
                    }
                    }

                    // finger pan + double-tap to reset (pinch handled above)
                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true   // pan the map, don't scroll the page
                        property real lastX
                        property real lastY
                        onPressed: { lastX = mouse.x; lastY = mouse.y }
                        onPositionChanged: {
                            canvas.panX += mouse.x - lastX
                            canvas.panY += mouse.y - lastY
                            lastX = mouse.x
                            lastY = mouse.y
                            canvas.requestPaint()
                        }
                        onDoubleClicked: mapBox.resetView()
                    }
                }

                // bottom overlay: faint scale bar on top, legend symbols below
                // it (dots in a row, each with its caption centred underneath)
                Column {
                    id: bottomOverlay
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.paddingMedium
                    spacing: Theme.paddingSmall

                    property real mpp: canvas.metersPerPx
                    property real niceM: page.niceDist(mpp * (mapBox.width * 0.28))
                    property real barPx: mpp > 0 ? niceM / mpp : 0

                    // --- scale bar (approximate), centred ---
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.paddingSmall
                        opacity: 0.45
                        visible: (monitor.gpsValid || monitor.estValid) && bottomOverlay.barPx > 0
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: bottomOverlay.barPx
                            height: 1
                            color: Theme.primaryColor
                            Rectangle { width: 1; height: Theme.paddingSmall; color: Theme.primaryColor; anchors.left: parent.left; anchors.bottom: parent.bottom }
                            Rectangle { width: 1; height: Theme.paddingSmall; color: Theme.primaryColor; anchors.right: parent.right; anchors.bottom: parent.bottom }
                        }
                        Label {
                            anchors.bottom: parent.bottom
                            text: "≈ " + page.fmtDist(bottomOverlay.niceM)
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.primaryColor
                        }
                    }

                    // --- legend: dots in a row, caption centred beneath each ---
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.paddingLarge
                        opacity: 0.7

                        Column {
                            spacing: 0
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter
                                spacing: 2
                                Label { text: "●"; color: "#3d6bff"; font.pixelSize: Theme.fontSizeExtraSmall }
                                Label { text: "→"; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeExtraSmall }
                                Label { text: "●"; color: "#ff4444"; font.pixelSize: Theme.fontSizeExtraSmall }
                            }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Signalstärke"); font.pixelSize: Theme.fontSizeTiny; color: Theme.secondaryColor }
                        }
                        Column {
                            spacing: 0
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "◯"; color: Theme.highlightColor; font.pixelSize: Theme.fontSizeExtraSmall }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Position"); font.pixelSize: Theme.fontSizeTiny; color: Theme.secondaryColor }
                        }
                        Column {
                            spacing: 0
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "⊕"; color: "#ffb300"; font.pixelSize: Theme.fontSizeExtraSmall }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Sender"); font.pixelSize: Theme.fontSizeTiny; color: Theme.secondaryColor }
                        }
                        Column {
                            visible: page.showTowers
                            spacing: 0
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "▲"; color: "#5fd08a"; font.pixelSize: Theme.fontSizeExtraSmall }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("OCID"); font.pixelSize: Theme.fontSizeTiny; color: Theme.secondaryColor }
                        }
                        Column {
                            visible: page.showLicensed
                            spacing: 0
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "◌"; color: "#7ec8ff"; font.pixelSize: Theme.fontSizeExtraSmall }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("BNetzA"); font.pixelSize: Theme.fontSizeTiny; color: Theme.secondaryColor }
                        }
                    }
                }

                // "up = where you point" hint while rotating (heading-up)
                Label {
                    visible: page.rotateWithCompass && sensor.active
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: Theme.paddingSmall
                    text: sensor.disturbed ? "⚠ " + qsTr("Kompass gestört (Metall in der Nähe?)")
                                           : "▲ " + qsTr("Blickrichtung")
                    color: sensor.disturbed ? "#ffb300" : Theme.highlightColor
                    font.pixelSize: Theme.fontSizeTiny
                }

                ViewPlaceholder {
                    enabled: !monitor.gpsEnabled
                    text: qsTr("GPS ist aus")
                    hintText: qsTr("Im Pulley-Menü einschalten")
                }
            }
        }
    }
}
