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

/*
 * OcidLookup — fetches known cell-tower locations from OpenCellID for the
 * "known towers" map overlay. Uses the getInArea endpoint (bounding box, max
 * 2x2 km, 5000 req/day on a free key). A real tower is in the database with
 * coordinates; a fake base station is not — so an unlisted serving cell is
 * itself a hint. The API key is stored with QSettings.
 */
#ifndef OCIDLOOKUP_H
#define OCIDLOOKUP_H

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class OcidLookup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList cells READ cells NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double lat READ lat NOTIFY changed)
    Q_PROPERTY(double lon READ lon NOTIFY changed)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY apiKeyChanged)

public:
    explicit OcidLookup(QObject *parent = nullptr);

    QVariantList cells() const { return m_cells; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    double lat() const { return m_lat; }
    double lon() const { return m_lon; }
    QString apiKey() const { return m_apiKey; }
    bool hasKey() const { return !m_apiKey.isEmpty(); }
    void setApiKey(const QString &k);

    // Fetch all known cells inside the bounding box (degrees).
    Q_INVOKABLE void fetchArea(double latMin, double lonMin,
                               double latMax, double lonMax);
    Q_INVOKABLE void clear();

signals:
    void changed();
    void apiKeyChanged();

private slots:
    void onFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_current;
    QVariantList m_cells;
    bool m_busy;
    QString m_status;
    QString m_apiKey;
    double m_lat;     // centre of the current cell set
    double m_lon;
};

#endif // OCIDLOOKUP_H
