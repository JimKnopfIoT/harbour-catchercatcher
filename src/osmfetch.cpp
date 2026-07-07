/*
  harbour-iwifi — WiFi security checker for Sailfish OS
  Copyright (C) 2026  JimKnopfIoT — GPLv3 or later.
*/
#include "osmfetch.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

OsmFetch::OsmFetch(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_current(0)
    , m_idx(0)
    , m_pendingLat(0)
    , m_pendingLon(0)
    , m_lat(0)
    , m_lon(0)
    , m_busy(false)
{
    // Public Overpass mirrors, tried in order. Since the query carries the user's
    // exact coordinates, we deliberately use only trusted OSM-community instances
    // and NO .ru host (privacy tool — no location data to Russian services). The
    // former default order buried the only working mirror behind two dead hosts
    // (each burning the 12 s timeout), which is why the map/buildings never loaded.
    // Verified reachable from the device (Jul 2026): openstreetmap.fr ~1.5 s,
    // overpass-api.de ~5 s; private.coffee/kumi.systems are kept only as last-
    // resort fallbacks (currently down).
    m_mirrors << "https://overpass.openstreetmap.fr/api/interpreter"
              << "https://overpass-api.de/api/interpreter"
              << "https://overpass.private.coffee/api/interpreter"
              << "https://overpass.kumi.systems/api/interpreter";

    connect(m_nam, &QNetworkAccessManager::finished, this, &OsmFetch::onFinished);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &OsmFetch::onTimeout);
}

void OsmFetch::fetch(double lat, double lon, double radius, int mode)
{
    if (m_current) {           // supersede any in-flight request
        m_timeout.stop();
        m_current->abort();
        m_current = 0;
    }
    m_pendingLat = lat;
    m_pendingLon = lon;

    const QString r = QString::number((int)radius);
    const QString la = QString::number(lat, 'f', 6);
    const QString lo = QString::number(lon, 'f', 6);
    const QString around = "(around:" + r + "," + la + "," + lo + ");";

    // Three tiers (see header). mode 0 (far) = only coarse/major roads — dropping
    // residential/service/foot/path ways keeps the way count (and paint cost) low
    // when zoomed way out. mode 1 adds land plots, mode 2 adds buildings.
    QString body;
    if (mode <= 0) {
        body = "way[\"highway\"~\"^(motorway|trunk|primary|secondary|tertiary)"
               "(_link)?$\"]" + around;
    } else {
        body = "way[\"highway\"]" + around;
        if (mode >= 2)
            body += "way[\"building\"]" + around;
        else
            body += "way[\"landuse\"]" + around;
    }

    m_query = "[out:json][timeout:25];(" + body + ");out geom;";

    m_idx = 0;
    m_busy = true;
    m_status = tr("Loading map…");
    emit changed();
    tryNext();
}

void OsmFetch::tryNext()
{
    if (m_idx >= m_mirrors.size()) {
        m_busy = false;
        m_status = tr("Map unavailable (no Overpass mirror reachable)");
        m_current = 0;
        emit changed();
        return;
    }
    QNetworkRequest req((QUrl(m_mirrors.at(m_idx))));
    req.setHeader(QNetworkRequest::UserAgentHeader, "harbour-iwifi");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    m_current = m_nam->post(req, m_query.toUtf8());
    m_timeout.start(12000);    // Qt 5.6 has no transferTimeout — do it by hand
}

void OsmFetch::onTimeout()
{
    if (m_current)
        m_current->abort();    // → onFinished with OperationCanceledError → next
}

void OsmFetch::onFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_current)    // a superseded/aborted-by-fetch reply
        return;
    m_timeout.stop();
    m_current = 0;

    if (reply->error() != QNetworkReply::NoError) {
        ++m_idx;               // this mirror failed — try the next one
        tryNext();
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonArray elements = root.value("elements").toArray();

    QVariantList ways;
    for (const QJsonValue &v : elements) {
        const QJsonObject el = v.toObject();
        if (el.value("type").toString() != "way")
            continue;
        const QJsonArray geom = el.value("geometry").toArray();
        if (geom.size() < 2)
            continue;
        QVariantList pts;
        for (const QJsonValue &g : geom) {
            const QJsonObject p = g.toObject();
            QVariantMap pt;
            pt.insert("lat", p.value("lat").toDouble());
            pt.insert("lon", p.value("lon").toDouble());
            pts.append(pt);
        }
        QVariantMap way;
        way.insert("building", el.value("tags").toObject().contains("building"));
        way.insert("pts", pts);
        ways.append(way);
    }

    m_ways = ways;
    m_lat = m_pendingLat;
    m_lon = m_pendingLon;
    m_busy = false;
    m_status = ways.isEmpty() ? tr("No map data here")
                              : tr("Map loaded (%1 ways)").arg(ways.size());
    emit changed();
}
