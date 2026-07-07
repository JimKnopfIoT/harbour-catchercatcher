import QtQuick 2.0
import QtPositioning 5.0
import Sailfish.Silica 1.0
import catchercatcher 1.0
import "pages"
import "cover"

ApplicationWindow
{
    id: app

    // Single shared backend instance for pages and cover.
    // All GPS state lives on the monitor so every page sees the same thing.
    property CellMonitor monitor: CellMonitor { }

    // hand the BNetzA licensed-site source to the backend for the Ghost-Site
    // heuristic (strong cell but no licensed macro site nearby -> hint). The
    // horizontal navigation strip
    //   [SMS-Begriffe] <- [SMS] <- [Main] -> [Map] -> [Begriffe]
    // is built as a chain of *attached* pages (see SMSGlossaryPage): attached
    // pages persist, so you can swipe freely both ways. Using push() instead
    // popped Main on the back-swipe, which is why you got stuck on SMS.
    Component.onCompleted: monitor.setBnetzaSource(bnetza)

    PositionSource {
        id: positionSource
        active: app.monitor.gpsEnabled
        updateInterval: 2000
        // satellite is most accurate, but allow network fixes so we get
        // *something* quickly (and indoors) instead of never turning valid.
        preferredPositioningMethods: PositionSource.AllPositioningMethods

        onPositionChanged: {
            if (position.latitudeValid && position.longitudeValid) {
                app.monitor.lat = position.coordinate.latitude
                app.monitor.lon = position.coordinate.longitude
                app.monitor.gpsValid = true
            }
        }
        onActiveChanged: if (!active) app.monitor.gpsValid = false
    }

    initialPage: Component { SMSGlossaryPage { monitor: app.monitor } }
    cover: Component { CoverPage { monitor: app.monitor } }
    allowedOrientations: defaultAllowedOrientations
}
