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

#include "ocidlookup.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>

OcidLookup::OcidLookup(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_current(nullptr)
    , m_busy(false)
    , m_lat(0.0)
    , m_lon(0.0)
{
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &OcidLookup::onFinished);
    QSettings s;
    m_apiKey = s.value(QStringLiteral("ocidApiKey")).toString();
}

void OcidLookup::setApiKey(const QString &k)
{
    const QString t = k.trimmed();
    if (t == m_apiKey) return;
    m_apiKey = t;
    QSettings s;
    s.setValue(QStringLiteral("ocidApiKey"), m_apiKey);
    emit apiKeyChanged();
}

void OcidLookup::clear()
{
    m_cells.clear();
    emit changed();
}

void OcidLookup::fetchArea(double latMin, double lonMin,
                           double latMax, double lonMax)
{
    if (m_apiKey.isEmpty()) {
        m_status = tr("Kein OpenCellID-API-Key");
        emit changed();
        return;
    }
    if (m_busy)
        return;

    // OpenCellID caps the box at 4,000,000 m² (~2x2 km). Shrink if needed.
    const double maxDeg = 0.018;   // ~2 km in latitude
    if (latMax - latMin > maxDeg) {
        const double c = (latMin + latMax) / 2;
        latMin = c - maxDeg / 2; latMax = c + maxDeg / 2;
    }
    if (lonMax - lonMin > maxDeg) {
        const double c = (lonMin + lonMax) / 2;
        lonMin = c - maxDeg / 2; lonMax = c + maxDeg / 2;
    }

    QUrl url(QStringLiteral("https://opencellid.org/cell/getInArea"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), m_apiKey);
    q.addQueryItem(QStringLiteral("BBOX"),
                   QStringLiteral("%1,%2,%3,%4")
                   .arg(latMin, 0, 'f', 6).arg(lonMin, 0, 'f', 6)
                   .arg(latMax, 0, 'f', 6).arg(lonMax, 0, 'f', 6));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("100"));
    url.setQuery(q);

    m_lat = (latMin + latMax) / 2;
    m_lon = (lonMin + lonMax) / 2;
    m_busy = true;
    m_status = tr("lade Masten…");
    emit changed();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("harbour-catchercatcher"));
    m_current = m_nam->get(req);
}

void OcidLookup::onFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    m_current = nullptr;
    m_busy = false;

    if (reply->error() != QNetworkReply::NoError) {
        m_status = tr("OpenCellID-Fehler: %1").arg(reply->errorString());
        emit changed();
        return;
    }

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        m_status = tr("Antwort unlesbar");
        emit changed();
        return;
    }
    const QJsonObject root = doc.object();

    QVariantList cells;
    const QJsonArray arr = root.value(QStringLiteral("cells")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject c = v.toObject();
        QVariantMap m;
        m.insert(QStringLiteral("lat"), c.value(QStringLiteral("lat")).toDouble());
        m.insert(QStringLiteral("lon"), c.value(QStringLiteral("lon")).toDouble());
        m.insert(QStringLiteral("mcc"), c.value(QStringLiteral("mcc")).toInt());
        m.insert(QStringLiteral("mnc"), c.value(QStringLiteral("mnc")).toInt());
        m.insert(QStringLiteral("lac"), c.value(QStringLiteral("lac")).toInt());
        m.insert(QStringLiteral("cid"), c.value(QStringLiteral("cellid")).toDouble());
        m.insert(QStringLiteral("radio"), c.value(QStringLiteral("radio")).toString());
        cells.append(m);
    }
    m_cells = cells;
    m_status = tr("%1 bekannte Masten").arg(cells.size());
    emit changed();
}
