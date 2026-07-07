/*
  harbour-iwifi — WiFi security checker for Sailfish OS
  Copyright (C) 2026  JimKnopfIoT — GPLv3 or later.

  Fetches a small OpenStreetMap vector tile (streets + buildings) around a GPS
  position from the Overpass API, for the opt-in radar background silhouette.
  Done in C++ (not QML XMLHttpRequest) because the device has no IPv6 and the
  main Overpass endpoint's IPv4 is flaky: this tries several mirror hosts in
  turn, with a per-request timeout, so one dead host can't stall the fetch.
*/
#ifndef OSMFETCH_H
#define OSMFETCH_H

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class OsmFetch : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList ways READ ways NOTIFY changed)
    Q_PROPERTY(double lat READ lat NOTIFY changed)
    Q_PROPERTY(double lon READ lon NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit OsmFetch(QObject *parent = nullptr);

    QVariantList ways() const { return m_ways; }
    double lat() const { return m_lat; }
    double lon() const { return m_lon; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    // Fetch OSM ways within `radius` m of (lat, lon). `mode` sets how much:
    //   2 = streets + buildings   (near view, heaviest)
    //   1 = streets + land plots  (mid ring, no buildings)
    //   0 = streets only          (far surroundings, lightest)
    // Splitting the three keeps a zoomed-out view smooth: buildings/plots stay
    // bounded to small radii, only cheap streets extend far out.
    Q_INVOKABLE void fetch(double lat, double lon, double radius, int mode = 2);

signals:
    void changed();

private slots:
    void onFinished(QNetworkReply *reply);
    void onTimeout();

private:
    void tryNext();

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_current;
    QTimer m_timeout;
    QStringList m_mirrors;
    int m_idx;
    QString m_query;
    double m_pendingLat;   // centre of the in-flight request
    double m_pendingLon;

    QVariantList m_ways;
    double m_lat;          // centre the current m_ways belong to
    double m_lon;
    bool m_busy;
    QString m_status;
};

#endif // OSMFETCH_H
