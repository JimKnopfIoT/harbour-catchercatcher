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

#include "bnetzalookup.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkCookieJar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QtMath>

#include <openssl/evp.h>

namespace {
const QString UA = QStringLiteral(
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120 Safari/537.36");
const QByteArray REF =
    "https://www.bundesnetzagentur.de/DE/Vportal/TK/Funktechnik/EMF/start.html";
const QString START_URL =
    QStringLiteral("https://www.bundesnetzagentur.de/DE/Vportal/TK/Funktechnik/EMF/start.html");
const QString JS_URL =
    QStringLiteral("https://www.bundesnetzagentur.de/emf-karte/js.asmx/jscontent?set=gsb2021");
const QString POST_URL =
    QStringLiteral("https://www.bundesnetzagentur.de/emf-karte/Standortservice.asmx/GetStandorteFreigabe");
const double  TILE = 0.02;               // ~2 km tile for the cached-area index
const qint64  TTL_SECS = 60LL * 24 * 3600;   // sites considered fresh for 60 days
}

BnetzaLookup::BnetzaLookup(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_busy(false)
    , m_retried(false)
    , m_lat(0.0)
    , m_lon(0.0)
    , m_bN(0), m_bE(0), m_bS(0), m_bW(0)
{
    m_nam->setCookieJar(new QNetworkCookieJar(this));
    loadCache();
}

QString BnetzaLookup::cacheDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// ---------------------------------------------------------------- cache ----

void BnetzaLookup::loadCache()
{
    QDir().mkpath(cacheDir());

    QFile f(cacheDir() + QStringLiteral("/bnetza-sites.ndjson"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QByteArray line = f.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            const int fid = o.value(QStringLiteral("fid")).toInt();
            if (fid == 0 || m_haveFid.contains(fid)) continue;
            m_haveFid.insert(fid);
            QVariantMap m;
            m.insert(QStringLiteral("lat"), o.value(QStringLiteral("lat")).toDouble());
            m.insert(QStringLiteral("lon"), o.value(QStringLiteral("lng")).toDouble());
            m.insert(QStringLiteral("fid"), fid);
            m.insert(QStringLiteral("titel"), o.value(QStringLiteral("t")).toString());
            m_sites.append(m);
        }
        f.close();
    }

    QFile t(cacheDir() + QStringLiteral("/bnetza-tiles.json"));
    if (t.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(t.readAll()).object();
        for (auto it = o.begin(); it != o.end(); ++it)
            m_tiles.insert(it.key(), static_cast<qint64>(it.value().toDouble()));
        t.close();
    }

    if (!m_sites.isEmpty())
        m_status = tr("%1 Standorte im Cache").arg(m_sites.size());
}

void BnetzaLookup::appendToCache(const QVariantList &fresh)
{
    if (fresh.isEmpty()) return;
    QFile f(cacheDir() + QStringLiteral("/bnetza-sites.ndjson"));
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    for (const QVariant &v : fresh) {
        const QVariantMap m = v.toMap();
        QJsonObject o;
        o.insert(QStringLiteral("fid"), m.value(QStringLiteral("fid")).toInt());
        o.insert(QStringLiteral("lat"), m.value(QStringLiteral("lat")).toDouble());
        o.insert(QStringLiteral("lng"), m.value(QStringLiteral("lon")).toDouble());
        o.insert(QStringLiteral("t"),   m.value(QStringLiteral("titel")).toString());
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.write("\n");
    }
    f.close();
}

void BnetzaLookup::saveTileIndex()
{
    QJsonObject o;
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it)
        o.insert(it.key(), static_cast<double>(it.value()));
    QFile t(cacheDir() + QStringLiteral("/bnetza-tiles.json"));
    if (t.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        t.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        t.close();
    }
}

bool BnetzaLookup::areaCached(double latMin, double lonMin,
                              double latMax, double lonMax) const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    const int i0 = qFloor(latMin / TILE), i1 = qFloor(latMax / TILE);
    const int j0 = qFloor(lonMin / TILE), j1 = qFloor(lonMax / TILE);
    for (int i = i0; i <= i1; ++i)
        for (int j = j0; j <= j1; ++j) {
            const auto it = m_tiles.find(QStringLiteral("%1_%2").arg(i).arg(j));
            if (it == m_tiles.end() || now - it.value() > TTL_SECS)
                return false;
        }
    return true;
}

void BnetzaLookup::markAreaFetched(double latMin, double lonMin,
                                   double latMax, double lonMax)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    const int i0 = qFloor(latMin / TILE), i1 = qFloor(latMax / TILE);
    const int j0 = qFloor(lonMin / TILE), j1 = qFloor(lonMax / TILE);
    for (int i = i0; i <= i1; ++i)
        for (int j = j0; j <= j1; ++j)
            m_tiles.insert(QStringLiteral("%1_%2").arg(i).arg(j), now);
    saveTileIndex();
}

bool BnetzaLookup::areaLoaded(double lat, double lon) const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    const auto it = m_tiles.find(
        QStringLiteral("%1_%2").arg(qFloor(lat / TILE)).arg(qFloor(lon / TILE)));
    return it != m_tiles.end() && now - it.value() <= TTL_SECS;
}

double BnetzaLookup::nearestSiteMeters(double lat, double lon) const
{
    double best = -1.0;
    const double cosLat = qCos(qDegreesToRadians(lat));
    for (const QVariant &v : m_sites) {
        const QVariantMap m = v.toMap();
        const double dLa = (m.value(QStringLiteral("lat")).toDouble() - lat) * 111320.0;
        const double dLo = (m.value(QStringLiteral("lon")).toDouble() - lon) * 111320.0 * cosLat;
        const double d = qSqrt(dLa * dLa + dLo * dLo);
        if (best < 0.0 || d < best) best = d;
    }
    return best;
}

QVariantMap BnetzaLookup::nearestSite(double lat, double lon) const
{
    QVariantMap best;
    double bestD = -1.0;
    const double cosLat = qCos(qDegreesToRadians(lat));
    for (const QVariant &v : m_sites) {
        const QVariantMap m = v.toMap();
        const double dLa = (m.value(QStringLiteral("lat")).toDouble() - lat) * 111320.0;
        const double dLo = (m.value(QStringLiteral("lon")).toDouble() - lon) * 111320.0 * cosLat;
        const double d = qSqrt(dLa * dLa + dLo * dLo);
        if (bestD < 0.0 || d < bestD) {
            bestD = d;
            best = m;
            best.insert(QStringLiteral("dist"), d);
        }
    }
    return best;
}

void BnetzaLookup::clearCache()
{
    QFile::remove(cacheDir() + QStringLiteral("/bnetza-sites.ndjson"));
    QFile::remove(cacheDir() + QStringLiteral("/bnetza-tiles.json"));
    m_sites.clear();
    m_haveFid.clear();
    m_tiles.clear();
    m_status = tr("Cache geleert");
    emit changed();
}

// -------------------------------------------------------------- network ----

void BnetzaLookup::fetchArea(double latMin, double lonMin,
                             double latMax, double lonMax, bool force)
{
    if (m_busy) return;
    m_lat = (latMin + latMax) / 2;
    m_lon = (lonMin + lonMax) / 2;

    if (!force && areaCached(latMin, lonMin, latMax, lonMax)) {
        m_status = tr("aus Cache (%1 Standorte)").arg(m_sites.size());
        emit changed();
        return;
    }

    m_bS = latMin; m_bW = lonMin; m_bN = latMax; m_bE = lonMax;
    m_busy = true;
    m_retried = false;
    m_status = tr("lade BNetzA-Standorte…");
    emit changed();

    if (m_password.isEmpty())
        startSession();
    else
        doPost();
}

void BnetzaLookup::startSession()
{
    QUrl url(START_URL);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, UA);
    req.setRawHeader("Referer", REF);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *r1 = m_nam->get(req);
    connect(r1, &QNetworkReply::finished, this, [this, r1]() {
        r1->deleteLater();
        if (r1->error() != QNetworkReply::NoError) {
            fail(tr("BNetzA-Verbindungsfehler")); return;
        }
        QUrl url2(JS_URL);
        QNetworkRequest req2(url2);
        req2.setHeader(QNetworkRequest::UserAgentHeader, UA);
        req2.setRawHeader("Referer", REF);
        req2.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        QNetworkReply *r2 = m_nam->get(req2);
        connect(r2, &QNetworkReply::finished, this, [this, r2]() {
            r2->deleteLater();
            if (r2->error() != QNetworkReply::NoError) {
                fail(tr("BNetzA-Verbindungsfehler")); return;
            }
            const QString js = QString::fromUtf8(r2->readAll());
            QRegularExpression re(
                QStringLiteral("var c=CryptoJS\\.enc\\.Utf8\\.parse\\(\"(.*?)\"\\)"));
            const QRegularExpressionMatch m = re.match(js);
            if (!m.hasMatch()) { fail(tr("BNetzA-Backend geändert")); return; }
            m_password = m.captured(1);
            doPost();
        });
    });
}

void BnetzaLookup::doPost()
{
    QJsonObject box;
    box.insert(QStringLiteral("nord"), m_bN);
    box.insert(QStringLiteral("ost"),  m_bE);
    box.insert(QStringLiteral("sued"), m_bS);
    box.insert(QStringLiteral("west"), m_bW);
    QJsonObject root;
    root.insert(QStringLiteral("Box"), box);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QUrl url(POST_URL);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, UA);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json; charset=UTF-8"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Referer", REF);
    QNetworkReply *r = m_nam->post(req, payload);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            fail(tr("BNetzA-Fehler: %1").arg(r->errorString())); return;
        }
        finishPost(r->readAll());
    });
}

void BnetzaLookup::finishPost(const QByteArray &body)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (doc.isNull() || !doc.isObject()) { fail(tr("Antwort unlesbar")); return; }
    const QJsonValue d = doc.object().value(QStringLiteral("d"));

    QJsonArray arr;
    if (d.isObject()) {
        const QJsonObject o = d.toObject();
        if (o.contains(QStringLiteral("Fehler"))) {
            const QString err = o.value(QStringLiteral("Fehler")).toString();
            if (err.contains(QStringLiteral("Session")) && !m_retried) {
                m_retried = true;
                m_password.clear();
                startSession();          // re-auth once and retry the POST
                return;
            }
            fail(tr("BNetzA: %1").arg(err));
            return;
        }
        if (o.value(QStringLiteral("SecMode")).toBool()) {
            const QByteArray plain =
                decryptResult(o.value(QStringLiteral("Result")).toString(), m_password);
            if (plain.isEmpty()) { fail(tr("Entschlüsselung fehlgeschlagen")); return; }
            arr = QJsonDocument::fromJson(plain).array();
        }
    } else if (d.isArray()) {
        arr = d.toArray();
    }

    QVariantList fresh;
    for (const QJsonValue &v : arr) {
        const QJsonObject s = v.toObject();
        const int fid = s.value(QStringLiteral("fID")).toInt();
        if (fid == 0 || m_haveFid.contains(fid)) continue;
        m_haveFid.insert(fid);
        QVariantMap m;
        m.insert(QStringLiteral("lat"),   s.value(QStringLiteral("Lat")).toDouble());
        m.insert(QStringLiteral("lon"),   s.value(QStringLiteral("Lng")).toDouble());
        m.insert(QStringLiteral("fid"),   fid);
        m.insert(QStringLiteral("titel"), s.value(QStringLiteral("Titel")).toString());
        fresh.append(m);
    }

    m_sites.append(fresh);
    appendToCache(fresh);
    markAreaFetched(m_bS, m_bW, m_bN, m_bE);

    m_busy = false;
    m_status = tr("%1 Standorte (%2 neu)").arg(m_sites.size()).arg(fresh.size());
    emit changed();
}

void BnetzaLookup::fail(const QString &msg)
{
    m_busy = false;
    m_status = msg;
    emit changed();
}

// -------------------------------------------------------------- crypto ----

QByteArray BnetzaLookup::decryptResult(const QString &b64, const QString &password)
{
    const QByteArray pw = password.toUtf8();
    static const char salt[] = "cryptography123example";   // 22 bytes
    unsigned char key[16];
    if (PKCS5_PBKDF2_HMAC_SHA1(pw.constData(), pw.size(),
            reinterpret_cast<const unsigned char *>(salt), sizeof(salt) - 1,
            1000, sizeof(key), key) != 1)
        return QByteArray();

    static const unsigned char iv[16] = {
        0xa5, 0xa8, 0xd2, 0xe9, 0xc1, 0x72, 0x1a, 0xe0,
        0xe8, 0x4a, 0xd6, 0x60, 0xc4, 0x72, 0xb1, 0xf3
    };

    const QByteArray ct = QByteArray::fromBase64(b64.toLatin1());
    if (ct.isEmpty() || ct.size() % 16 != 0) return QByteArray();

    QByteArray out(ct.size() + 16, '\0');
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return QByteArray();

    QByteArray result;
    int outl = 0, total = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, key, iv) == 1 &&
        EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &outl,
                          reinterpret_cast<const unsigned char *>(ct.constData()),
                          ct.size()) == 1) {
        total = outl;
        if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + total,
                                &outl) == 1) {
            total += outl;
            result = QByteArray(out.constData(), total);
        }
    }
    EVP_CIPHER_CTX_free(ctx);
    return result;
}
