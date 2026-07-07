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
 * BnetzaLookup — fetches licensed radio-site locations (Standortbescheinigungen)
 * from the Bundesnetzagentur EMF map for the "authority sites" overlay. Unlike
 * OpenCellID these are the *official* macro sites (no crowdsourcing gaps), but
 * they carry no cell-IDs and their coordinates are privacy-rounded — hence the
 * map draws them as dashed uncertainty circles, not exact pins.
 *
 * The EMF map has an undocumented ASP.NET JSON backend:
 *   1. GET start page          -> session cookies
 *   2. GET js.asmx/jscontent   -> the (per-session) AES password
 *   3. POST Standortservice.asmx/GetStandorteFreigabe {Box:{nord,ost,sued,west}}
 *      -> {"d":{"SecMode":true,"Result":"<base64 AES-128-CBC>"}}
 *   4. decrypt (PBKDF2-HMAC-SHA1 + AES-128-CBC, fixed IV) -> [{Titel,Lng,Lat,fID}]
 *
 * Results are cached to NDJSON in the app data dir and deduped by fID, so once
 * an area is loaded it survives restarts and is available offline. A tile index
 * records which ~2 km tiles were fetched (+ when) so re-loading a fresh area is
 * a no-op.
 */
#ifndef BNETZALOOKUP_H
#define BNETZALOOKUP_H

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QSet>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class BnetzaLookup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList sites READ sites NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double lat READ lat NOTIFY changed)
    Q_PROPERTY(double lon READ lon NOTIFY changed)

public:
    explicit BnetzaLookup(QObject *parent = nullptr);

    QVariantList sites() const { return m_sites; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    double lat() const { return m_lat; }
    double lon() const { return m_lon; }

    // Fetch licensed sites inside the bounding box (degrees). With force=false a
    // box whose tiles are all cached & fresh is served from cache (no network).
    Q_INVOKABLE void fetchArea(double latMin, double lonMin,
                               double latMax, double lonMax, bool force);
    // Drop the on-disk cache and the in-memory sites.
    Q_INVOKABLE void clearCache();

    // For the Ghost-Site heuristic (CellMonitor). areaLoaded() is true only when
    // the tile at (lat,lon) was actually fetched & is fresh — so "no site nearby"
    // can be distinguished from "no data here". nearestSiteMeters() returns the
    // distance to the closest cached site, or -1 if none are cached.
    Q_INVOKABLE bool areaLoaded(double lat, double lon) const;
    Q_INVOKABLE double nearestSiteMeters(double lat, double lon) const;
    // nearest cached site to (lat,lon): {lat, lon, fid, titel, dist(m)}; empty if none.
    Q_INVOKABLE QVariantMap nearestSite(double lat, double lon) const;

signals:
    void changed();

private:
    void loadCache();
    void appendToCache(const QVariantList &fresh);
    void saveTileIndex();
    bool areaCached(double latMin, double lonMin, double latMax, double lonMax) const;
    void markAreaFetched(double latMin, double lonMin, double latMax, double lonMax);

    // network chain
    void startSession();          // step 1+2
    void doPost();                // step 3 (+ decrypt)
    void finishPost(const QByteArray &body);
    void fail(const QString &msg);

    static QByteArray decryptResult(const QString &b64, const QString &password);

    QString cacheDir() const;

    QNetworkAccessManager *m_nam;
    QString m_password;           // per-session, empty until session established
    bool m_busy;
    bool m_retried;               // one re-auth retry on expired session
    QString m_status;
    double m_lat;                 // centre of the last requested box
    double m_lon;

    // pending request box
    double m_bN, m_bE, m_bS, m_bW;

    QVariantList m_sites;         // union of all cached sites
    QSet<int> m_haveFid;          // dedupe
    QHash<QString, qint64> m_tiles;   // tileKey -> epoch seconds fetched
};

#endif // BNETZALOOKUP_H
