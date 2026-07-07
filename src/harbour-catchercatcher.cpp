/*
  harbour-catchercatcher — IMSI-catcher detector for Sailfish OS
  Copyright (C) 2026  JimKnopfIoT

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version. This program is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License <https://www.gnu.org/licenses/> for details.
*/

#include <QtQuick>
#include <QQmlContext>
#include <sailfishapp.h>
#include "cellmonitor.h"
#include "osmfetch.h"
#include "ocidlookup.h"
#include "bnetzalookup.h"
#include "sensorreader.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    // Stable names so QSettings (whitelist) lands in a predictable place.
    app->setOrganizationName(QStringLiteral("catchercatcher"));
    app->setOrganizationDomain(QStringLiteral("catchercatcher"));
    app->setApplicationName(QStringLiteral("harbour-catchercatcher"));

    qmlRegisterType<CellMonitor>("catchercatcher", 1, 0, "CellMonitor");

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    // OSM silhouette (reused from harbour-iwifi). Two layers: `osm` is the
    // detailed near view (streets + buildings), `osmWide` is the coarse
    // surroundings (streets + plots, no buildings) shown when zoomed out.
    QScopedPointer<OsmFetch> osm(new OsmFetch(view.data()));
    view->rootContext()->setContextProperty(QStringLiteral("osm"), osm.data());
    QScopedPointer<OsmFetch> osmWide(new OsmFetch(view.data()));
    view->rootContext()->setContextProperty(QStringLiteral("osmWide"), osmWide.data());
    QScopedPointer<OsmFetch> osmFar(new OsmFetch(view.data()));
    view->rootContext()->setContextProperty(QStringLiteral("osmFar"), osmFar.data());

    // OpenCellID known-tower lookup for the "towers" map overlay.
    QScopedPointer<OcidLookup> ocid(new OcidLookup(view.data()));
    view->rootContext()->setContextProperty(QStringLiteral("ocid"), ocid.data());

    // BNetzA EMF licensed-site lookup for the "authority sites" overlay.
    QScopedPointer<BnetzaLookup> bnetza(new BnetzaLookup(view.data()));
    view->rootContext()->setContextProperty(QStringLiteral("bnetza"), bnetza.data());

    // Relative heading (gyro+accel, like iBT) so the map can rotate heading-up.
    QScopedPointer<SensorReader> sensor(new SensorReader(view.data()));
    view->rootContext()->setContextProperty(QStringLiteral("sensor"), sensor.data());

    view->setSource(SailfishApp::pathTo("qml/harbour-catchercatcher.qml"));
    view->show();

    return app->exec();
}
