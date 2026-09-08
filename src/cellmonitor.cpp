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

#include "cellmonitor.h"
#include "bnetzalookup.h"

#include <QtMath>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QCoreApplication>
#include <QDebug>

#include <fcntl.h>
#include <unistd.h>

static const QString OFONO = QStringLiteral("org.ofono");

// ofono demarshals a{sv} into a QVariantMap; inner values usually arrive
// already unwrapped, but be defensive about the 'v' wrapper just in case.
static QVariant unwrap(const QVariant &v)
{
    if (v.canConvert<QDBusVariant>())
        return v.value<QDBusVariant>().variant();
    return v;
}

static QString mapStr(const QVariantMap &m, const QString &key)
{
    if (!m.contains(key)) return QString();
    return unwrap(m.value(key)).toString();
}

static int mapInt(const QVariantMap &m, const QString &key, int fallback = -1)
{
    if (!m.contains(key)) return fallback;
    return unwrap(m.value(key)).toInt();
}

CellMonitor::CellMonitor(QObject *parent)
    : QObject(parent)
{
    m_modemPath = QStringLiteral("/ril_0");   // Xperia 10 III / binder RIL
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &CellMonitor::poll);

    const QString dir = QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_logPath = dir + QStringLiteral("/catchercatcher.log");
    m_smsLogPath = dir + QStringLiteral("/sms-events.ndjson");

    loadWhitelist();
    loadSmsEvents();

    // restore persisted GPS + map-view preferences from the last session
    {
        QSettings s;
        m_gpsEnabled  = s.value(QStringLiteral("gpsEnabled"), false).toBool();
        m_mapStreets  = s.value(QStringLiteral("mapStreets"), false).toBool();
        m_mapTowers   = s.value(QStringLiteral("mapTowers"), false).toBool();
        m_mapLicensed = s.value(QStringLiteral("mapLicensed"), false).toBool();
        m_mapRotate   = s.value(QStringLiteral("mapRotate"), false).toBool();
    }

    // Live incoming-SMS capture from ofono, for metadata browsing. Deep PDU
    // fields (PID/DCS/binary) still need Tier 2; here we get sender/time/class.
    QDBusConnection::systemBus().connect(
        OFONO, m_modemPath, QStringLiteral("org.ofono.MessageManager"),
        QStringLiteral("IncomingMessage"), this,
        SLOT(onIncomingMessage(QString, QVariantMap)));
    QDBusConnection::systemBus().connect(
        OFONO, m_modemPath, QStringLiteral("org.ofono.MessageManager"),
        QStringLiteral("ImmediateMessage"), this,
        SLOT(onImmediateMessage(QString, QVariantMap)));

    // Voice-call state, to tell a benign CSFB downgrade (a CS call pulls the
    // modem to 2G/3G when VoLTE is off) apart from a real catcher downgrade.
    QDBusConnection::systemBus().connect(
        OFONO, m_modemPath, QStringLiteral("org.ofono.VoiceCallManager"),
        QStringLiteral("CallAdded"), this,
        SLOT(onCallAdded(QDBusObjectPath, QVariantMap)));
    QDBusConnection::systemBus().connect(
        OFONO, m_modemPath, QStringLiteral("org.ofono.VoiceCallManager"),
        QStringLiteral("CallRemoved"), this,
        SLOT(onCallRemoved(QDBusObjectPath)));
}

void CellMonitor::onCallAdded(const QDBusObjectPath &path, const QVariantMap &)
{
    m_activeCalls.insert(path.path());
}

void CellMonitor::onCallRemoved(const QDBusObjectPath &path)
{
    if (m_activeCalls.remove(path.path()) && m_activeCalls.isEmpty())
        m_lastCallEndMs = QDateTime::currentMSecsSinceEpoch();
}

// True while a CS voice call is up, or within a grace window after the last
// one ended — the span in which a 2G/3G downgrade is expected CSFB, not attack.
bool CellMonitor::inCsfbWindow() const
{
    if (!m_activeCalls.isEmpty())
        return true;
    return m_lastCallEndMs > 0 &&
           (QDateTime::currentMSecsSinceEpoch() - m_lastCallEndMs) < 25000;
}

void CellMonitor::onIncomingMessage(const QString &message, const QVariantMap &info)
{
    // An empty incoming SMS is suspicious: on this device ofono surfaces a Type-0
    // (silent ping) as a visible *empty* message. Flag it as a warning rather than
    // letting it pass as a harmless SMS.
    const bool empty = message.trimmed().isEmpty();
    QVariantMap ev;
    ev.insert(QStringLiteral("type"),
              empty ? tr("Leere SMS (verdächtig)") : tr("SMS (eingehend)"));
    ev.insert(QStringLiteral("severity"), empty ? 2 : 0);
    ev.insert(QStringLiteral("from"), unwrap(info.value(QStringLiteral("Sender"))).toString());
    ev.insert(QStringLiteral("sent"), unwrap(info.value(QStringLiteral("SentTime"))).toString());
    if (empty)
        ev.insert(QStringLiteral("dcs"),
                  tr("leerer Text — möglicher stiller Ping (Type-0)"));
    ev.insert(QStringLiteral("text"), message);
    addSmsEvent(ev);
}

void CellMonitor::onImmediateMessage(const QString &message, const QVariantMap &info)
{
    QVariantMap ev;
    ev.insert(QStringLiteral("type"), tr("Flash-SMS (Class 0)"));
    ev.insert(QStringLiteral("severity"), 2);
    ev.insert(QStringLiteral("from"), unwrap(info.value(QStringLiteral("Sender"))).toString());
    ev.insert(QStringLiteral("dcs"), QStringLiteral("Class 0 (Flash)"));
    ev.insert(QStringLiteral("sent"), unwrap(info.value(QStringLiteral("SentTime"))).toString());
    ev.insert(QStringLiteral("text"), message);
    addSmsEvent(ev);
}

int CellMonitor::techRank(const QString &tech)
{
    const QString t = tech.toLower();
    if (t == QLatin1String("nr") || t.contains(QLatin1String("5g"))) return 4;
    if (t == QLatin1String("lte")) return 3;
    if (t.contains(QLatin1String("umts")) || t.contains(QLatin1String("hs")) ||
        t == QLatin1String("wcdma") || t == QLatin1String("edge")) return 2;
    if (t == QLatin1String("gsm") || t == QLatin1String("gprs")) return 1;
    return 0;
}

void CellMonitor::start()
{
    if (m_monitoring) return;
    m_monitoring = true;
    emit monitoringChanged();
    addEvent(0, tr("Überwachung gestartet"));
    poll();
    m_timer.start();
}

void CellMonitor::stop()
{
    if (!m_monitoring) return;
    m_monitoring = false;
    m_timer.stop();
    emit monitoringChanged();
    addEvent(0, tr("Überwachung gestoppt"));
}

void CellMonitor::poll()
{
    QDBusConnection bus = QDBusConnection::systemBus();

    // These are SYNCHRONOUS D-Bus calls on the GUI thread. ofono's default
    // reply timeout is 25 s, and on this device NetworkMonitor.
    // GetServingCellInformation reliably takes its full ~10 s internal timeout
    // when the modem is on NR/5G (the RIL doesn't return serving-cell info) —
    // which would freeze the UI every poll and make Lipstick show
    // "app not responding". Cap every call to a short timeout so a stalled
    // ofono method can never block the UI for more than a fraction of a second;
    // a timed-out call just yields an invalid reply, which the merge logic
    // below already tolerates (it falls back to NetworkRegistration data).
    const int kFastTimeoutMs = 800;

    QDBusInterface netReg(OFONO, m_modemPath,
                          QStringLiteral("org.ofono.NetworkRegistration"), bus);
    netReg.setTimeout(kFastTimeoutMs);
    QDBusReply<QVariantMap> regReply = netReg.call(QStringLiteral("GetProperties"));
    if (!regReply.isValid()) {
        if (m_available) { m_available = false; emit availableChanged(); }
        return;
    }
    if (!m_available) { m_available = true; emit availableChanged(); }
    const QVariantMap reg = regReply.value();

    QDBusInterface netMon(OFONO, m_modemPath,
                          QStringLiteral("org.ofono.NetworkMonitor"), bus);
    netMon.setTimeout(kFastTimeoutMs);
    QDBusReply<QVariantMap> servReply =
            netMon.call(QStringLiteral("GetServingCellInformation"));
    const QVariantMap serv = servReply.isValid() ? servReply.value() : QVariantMap();

    int neighbours = -1;
    QDBusInterface cellInfo(OFONO, m_modemPath,
                            QStringLiteral("org.nemomobile.ofono.CellInfo"), bus);
    cellInfo.setTimeout(kFastTimeoutMs);
    QDBusReply<QList<QDBusObjectPath> > cellsReply =
            cellInfo.call(QStringLiteral("GetCells"));
    if (cellsReply.isValid())
        neighbours = cellsReply.value().size();

    QDBusInterface radio(OFONO, m_modemPath,
                         QStringLiteral("org.ofono.RadioSettings"), bus);
    radio.setTimeout(kFastTimeoutMs);
    QDBusReply<QVariantMap> radioReply = radio.call(QStringLiteral("GetProperties"));
    if (radioReply.isValid()) {
        const QString pref = mapStr(radioReply.value(),
                                    QStringLiteral("TechnologyPreference"));
        if (pref != m_techPref) m_techPref = pref;   // emitted via cellChanged
    }

    // --- merge the two views (NetworkMonitor is richer where present) -----
    const QString tech = mapStr(serv, QStringLiteral("Technology")).isEmpty()
            ? mapStr(reg, QStringLiteral("Technology"))
            : mapStr(serv, QStringLiteral("Technology"));
    const QString op   = mapStr(reg, QStringLiteral("Name"));
    const QString mcc  = mapStr(serv, QStringLiteral("MobileCountryCode")).isEmpty()
            ? mapStr(reg, QStringLiteral("MobileCountryCode"))
            : mapStr(serv, QStringLiteral("MobileCountryCode"));
    const QString mnc  = mapStr(serv, QStringLiteral("MobileNetworkCode")).isEmpty()
            ? mapStr(reg, QStringLiteral("MobileNetworkCode"))
            : mapStr(serv, QStringLiteral("MobileNetworkCode"));
    const uint cid = uint(mapInt(reg, QStringLiteral("CellId"),
                                 mapInt(serv, QStringLiteral("CellId"), 0)));
    int lac = mapInt(serv, QStringLiteral("LocationAreaCode"),
                     mapInt(reg, QStringLiteral("LocationAreaCode"), -1));
    if (lac < 0)
        lac = mapInt(serv, QStringLiteral("TrackingAreaCode"), -1);
    const int earfcn = mapInt(serv, QStringLiteral("EARFCN"),
                              mapInt(serv, QStringLiteral("ARFCN"), -1));
    const int rsrp = mapInt(serv, QStringLiteral("ReferenceSignalReceivedPower"), -1);
    const int rsrq = mapInt(serv, QStringLiteral("ReferenceSignalReceivedQuality"), -1);
    const int strength = mapInt(serv, QStringLiteral("Strength"),
                                mapInt(reg, QStringLiteral("Strength"), -1));

    const bool changed =
            tech != m_technology || op != m_operatorName || cid != m_cellId ||
            lac != m_lac || earfcn != m_earfcn || rsrp != m_rsrp ||
            rsrq != m_rsrq || strength != m_strength || neighbours != m_neighbourCount;

    m_technology = tech;
    m_operatorName = op;
    m_mcc = mcc;
    m_mnc = mnc;
    m_cellId = cid;
    m_lac = lac;
    m_earfcn = earfcn;
    m_rsrp = rsrp;
    m_rsrq = rsrq;
    m_strength = strength;
    m_neighbourCount = neighbours;
    if (changed)
        emit cellChanged();

    // --- heuristics -------------------------------------------------------
    // A cell the user has explicitly trusted (whitelist) is never flagged: no
    // downgrade alarm, no ghost/neighbour warning. "Wer in der Whitelist ist,
    // bei dem wird nicht gewarnt."
    const bool trusted = currentCellTrusted();

    // Is the *current* signal strong? A real catcher pulls you down with a
    // strong local signal; a benign coverage-driven downgrade (basement, cell
    // edge) happens when the signal is weak. We only alarm on strong signals.
    const bool strongSignal = (m_rsrp != -1 && m_rsrp >= -100) ||
                              (m_rsrp == -1 && m_strength >= 40);

    if (m_havePrev && cid != 0 && !trusted) {
        const int prevRank = techRank(m_prevTech);
        const int newRank = techRank(tech);
        // Only a drop to 3G/2G is catcher-relevant. NR<->LTE flip-flops are
        // normal NSA behaviour and must never raise an alarm.
        if (newRank > 0 && prevRank >= 3 && newRank <= 2) {
            if (inCsfbWindow()) {
                // A CS voice call (VoLTE off) forces CSFB to 2G/3G — expected,
                // not an attack. Note it, but never alarm.
                addEvent(0, tr("Herabstufung %1 → %2 während Anruf (CSFB, normal)")
                         .arg(m_prevTech.toUpper(), tech.toUpper()));
            } else if (strongSignal) {
                // forced down to 3G/2G at strong signal — classic catcher pull.
                const int sev = newRank == 1 ? 3 : 2;
                raiseThreat(sev, tr("Herabstufung von %1 auf %2")
                            .arg(m_prevTech.toUpper(), tech.toUpper()));
            } else {
                // weak signal -> almost certainly a coverage hole, not an attack.
                addEvent(1, tr("Herabstufung %1 → %2 bei schwachem Signal "
                               "(vermutlich Funkloch)")
                         .arg(m_prevTech.toUpper(), tech.toUpper()));
            }
        }

        if (cid != m_prevCellId && cid != 0) {
            if (!m_seenCells.contains(cid)) {
                addEvent(1, tr("Neue Zelle: %1 (%2, %3)")
                         .arg(cid).arg(op, tech.toUpper()));
            }
            if (m_prevLac >= 0 && lac >= 0 && lac != m_prevLac &&
                op == m_operatorName) {
                addEvent(2, tr("LAC/TAC-Wechsel %1 → %2 ohne Betreiberwechsel")
                         .arg(m_prevLac).arg(lac));
            }
        }
    }

    // A serving cell that persistently shows no neighbours is a soft signal.
    if (neighbours == 0)
        ++m_noNeighbourStreak;
    else if (neighbours > 0)
        m_noNeighbourStreak = 0;
    if (m_noNeighbourStreak == 3 && !trusted)
        addEvent(2, tr("Serving-Zelle ohne sichtbare Nachbarzellen"));

    // Whitelist: once the user has trusted some cells, flag any cell that is
    // not among them (a strong hint when combined with the signals above).
    if (cid != 0 && cid != m_prevCellId && !m_whitelist.isEmpty() && !trusted &&
        !m_whitelist.contains(cellKey())) {
        addEvent(1, tr("Zelle nicht in Whitelist: %1").arg(cellKey()));
    }

    // Ghost-site: a strong serving cell with no licensed BNetzA site nearby is
    // suspect — a catcher rarely sits on a licensed macro site. Runs once per new
    // cell, and only where authority data is actually loaded for this spot, so
    // "no data" is never mistaken for "no site". Info-level only: coordinates are
    // privacy-rounded and small/new sites may be missing — a hint, not proof.
    if (m_bnetza && cid != 0 && cid != m_ghostCheckedCell && !trusted &&
        m_gpsValid && m_lat != 0.0 && m_lon != 0.0 &&
        m_bnetza->areaLoaded(m_lat, m_lon)) {
        const bool strong = (m_rsrp != -1 && m_rsrp >= -85) ||
                            (m_rsrp == -1 && m_strength >= 70);
        bool ghost = false;
        if (strong) {
            const double d = m_bnetza->nearestSiteMeters(m_lat, m_lon);
            if (d < 0.0) {
                addEvent(1, tr("Starke Zelle, aber kein lizenzierter BNetzA-Standort im geladenen Bereich"));
                ghost = true;
            } else if (d > 1500.0) {
                addEvent(1, tr("Starke Zelle, nächster BNetzA-Standort erst %1 m entfernt").arg(int(d)));
                ghost = true;
            }
        }
        // Remember the verdict so the map's snap-to-mast is suppressed for this
        // cell: snapping a ghost-suspect estimate onto a real licensed mast
        // would visually park the catcher on legitimate infrastructure.
        m_ghostFlaggedCell = ghost ? cid : 0;
        m_ghostCheckedCell = cid;
    }

    // Auto-protect: on a real alert, steer the modem to LTE-only so it cannot
    // be pulled down onto a 2G/3G catcher. Opt-in.
    if (m_autoProtect && m_threatLevel >= 3 &&
        m_techPref != QLatin1String("lte")) {
        lockLteOnly();
    }

    // Collect a map sample whenever we have both a GPS fix and a cell.
    if (cid != 0 && m_lat != 0.0 && m_lon != 0.0)
        addSample();

    // identity (2 sync D-Bus calls) only until we have it once, then rarely —
    // running it every poll blocked the UI thread and made the app lag.
    if (!m_identityChecked || (m_identityRecheck++ % 24) == 0)
        readIdentity();

    if (m_logging)
        writeLog(QStringLiteral("cell"), QString());

    if (cid != 0)
        m_seenCells.insert(cid);
    m_havePrev = true;
    m_prevTech = tech;
    m_prevCellId = cid;
    m_prevLac = lac;
}

void CellMonitor::setBnetzaSource(QObject *b)
{
    m_bnetza = qobject_cast<BnetzaLookup *>(b);
    // re-evaluate the sender estimate when authority data arrives/updates, so the
    // snap-to-nearest-site happens without waiting for the next movement sample.
    if (m_bnetza)
        connect(m_bnetza, &BnetzaLookup::changed, this,
                [this]() { recomputeEstimate(); });
}

void CellMonitor::addEvent(int level, const QString &text)
{
    QVariantMap e;
    e.insert(QStringLiteral("time"),
             QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
    e.insert(QStringLiteral("level"), level);
    e.insert(QStringLiteral("text"), text);
    m_events.prepend(e);
    while (m_events.size() > 200)
        m_events.removeLast();
    emit eventsChanged();
    if (m_logging)
        writeLog(QStringLiteral("event"), text);
    if (level >= 2)
        raiseThreat(level, text);
}

void CellMonitor::raiseThreat(int level, const QString &text)
{
    // addEvent already logged warnings/alerts; log info-level raises here only.
    if (level < 2)
        addEvent(level, text);
    if (level > m_threatLevel) {
        m_threatLevel = level;
        m_threatText = text;
        emit threatChanged();
    }
}

void CellMonitor::acknowledge()
{
    if (m_threatLevel == 0 && m_threatText.isEmpty()) return;
    m_threatLevel = 0;
    m_threatText.clear();
    emit threatChanged();
}

void CellMonitor::clearEvents()
{
    m_events.clear();
    emit eventsChanged();
}

// ---------------------------------------------------------------------------
// Position feed (from a QML PositionSource) — used by the log and the map.
// ---------------------------------------------------------------------------
void CellMonitor::setLat(double v) { if (v != m_lat) { m_lat = v; emit posChanged(); } }
void CellMonitor::setLon(double v) { if (v != m_lon) { m_lon = v; emit posChanged(); } }

void CellMonitor::setGpsEnabled(bool v)
{
    if (v == m_gpsEnabled) return;
    m_gpsEnabled = v;
    if (!v) m_gpsValid = false;
    QSettings().setValue(QStringLiteral("gpsEnabled"), v);
    emit gpsChanged();
}

// persisted map-view toggles — remembered across page switches and restarts
void CellMonitor::setMapStreets(bool v)
{
    if (v == m_mapStreets) return;
    m_mapStreets = v;
    QSettings().setValue(QStringLiteral("mapStreets"), v);
    emit mapPrefsChanged();
}
void CellMonitor::setMapTowers(bool v)
{
    if (v == m_mapTowers) return;
    m_mapTowers = v;
    QSettings().setValue(QStringLiteral("mapTowers"), v);
    emit mapPrefsChanged();
}
void CellMonitor::setMapLicensed(bool v)
{
    if (v == m_mapLicensed) return;
    m_mapLicensed = v;
    QSettings().setValue(QStringLiteral("mapLicensed"), v);
    emit mapPrefsChanged();
}
void CellMonitor::setMapRotate(bool v)
{
    if (v == m_mapRotate) return;
    m_mapRotate = v;
    QSettings().setValue(QStringLiteral("mapRotate"), v);
    emit mapPrefsChanged();
}

void CellMonitor::setGpsValid(bool v)
{
    if (v == m_gpsValid) return;
    m_gpsValid = v;
    emit gpsChanged();
}

// ---------------------------------------------------------------------------
// Logging — append one NDJSON record per call, everything we know about the
// current cell plus the GPS fix (so the map/analysis can localise later).
// ---------------------------------------------------------------------------
void CellMonitor::setLogging(bool on)
{
    if (on == m_logging) return;
    m_logging = on;
    emit settingsChanged();
    if (on) {
        addEvent(0, tr("Logging aktiv: %1").arg(m_logPath));
        writeLog(QStringLiteral("start"), QStringLiteral("logging enabled"));
    } else {
        addEvent(0, tr("Logging gestoppt"));
    }
}

static QString jsonEscape(const QString &s)
{
    QString o;
    o.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '"':  o += QLatin1String("\\\""); break;
        case '\\': o += QLatin1String("\\\\"); break;
        case '\n': o += QLatin1String("\\n");  break;
        case '\r': o += QLatin1String("\\r");  break;
        case '\t': o += QLatin1String("\\t");  break;
        case '\b': o += QLatin1String("\\b");  break;
        case '\f': o += QLatin1String("\\f");  break;
        default:
            // Escape any remaining C0 control char (0x00-0x1F) as \uXXXX; a raw
            // control char in a value would otherwise corrupt the NDJSON line
            // and make QJsonDocument::fromJson() drop it (and the rest) on reload.
            if (c.unicode() < 0x20)
                o += QStringLiteral("\\u%1").arg(c.unicode(), 4, 16, QLatin1Char('0'));
            else
                o += c;
        }
    }
    return o;
}

// Keep a persisted NDJSON log from growing without bound: once it passes
// maxBytes, roll it to "<path>.1" (replacing any earlier roll) and start a
// fresh file. Bounds disk use to ~2*maxBytes while keeping recent history.
static void rotateIfLarge(const QString &path, qint64 maxBytes)
{
    if (QFileInfo(path).size() < maxBytes)
        return;
    const QString bak = path + QStringLiteral(".1");
    QFile::remove(bak);
    QFile::rename(path, bak);
}

void CellMonitor::writeLog(const QString &kind, const QString &text)
{
    rotateIfLarge(m_logPath, 4 * 1024 * 1024);   // cap the cell log at ~4 MB
    QFile f(m_logPath);
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts << "{"
       << "\"ts\":\"" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\","
       << "\"kind\":\"" << kind << "\","
       << "\"op\":\"" << jsonEscape(m_operatorName) << "\","
       << "\"mcc\":\"" << m_mcc << "\",\"mnc\":\"" << m_mnc << "\","
       << "\"tech\":\"" << m_technology << "\","
       << "\"cid\":" << m_cellId << ",\"enb\":" << enbId() << ",\"lac\":" << m_lac << ","
       << "\"earfcn\":" << m_earfcn << ",\"rsrp\":" << m_rsrp << ",\"rsrq\":" << m_rsrq << ","
       << "\"strength\":" << m_strength << ",\"neighbours\":" << m_neighbourCount << ","
       << "\"techpref\":\"" << m_techPref << "\","
       << "\"threat\":" << m_threatLevel << ","
       << "\"lat\":" << QString::number(m_lat, 'f', 6) << ","
       << "\"lon\":" << QString::number(m_lon, 'f', 6) << ","
       << "\"trusted\":" << (currentCellTrusted() ? "true" : "false");
    if (!text.isEmpty())
        ts << ",\"text\":\"" << jsonEscape(text) << "\"";
    ts << "}\n";
    f.close();
}

// ---------------------------------------------------------------------------
// Mitigation — steer the modem via ofono so we avoid a detected catcher.
// ---------------------------------------------------------------------------
void CellMonitor::ofonoSetProperty(const QString &iface, const QString &prop,
                                   const QVariant &value)
{
    QDBusInterface i(OFONO, m_modemPath, iface, QDBusConnection::systemBus());
    QDBusReply<void> r = i.call(QStringLiteral("SetProperty"), prop,
                                QVariant::fromValue(QDBusVariant(value)));
    if (!r.isValid())
        addEvent(1, tr("ofono %1.%2 fehlgeschlagen: %3")
                 .arg(iface.section('.', -1), prop, r.error().message()));
}

void CellMonitor::setAutoProtect(bool on)
{
    if (on == m_autoProtect) return;
    m_autoProtect = on;
    emit settingsChanged();
    addEvent(0, on ? tr("Auto-Schutz aktiv (LTE-only bei Alarm)")
                   : tr("Auto-Schutz aus"));
}

void CellMonitor::setTechnologyPreference(const QString &tech)
{
    ofonoSetProperty(QStringLiteral("org.ofono.RadioSettings"),
                     QStringLiteral("TechnologyPreference"), tech);
    addEvent(0, tr("Funktechnik gesetzt: %1").arg(tech.toUpper()));
}

void CellMonitor::lockLteOnly()
{
    setTechnologyPreference(QStringLiteral("lte"));
}

void CellMonitor::allowAllRat()
{
    setTechnologyPreference(QStringLiteral("nr"));
}

void CellMonitor::goOffline()
{
    ofonoSetProperty(QStringLiteral("org.ofono.Modem"),
                     QStringLiteral("Online"), false);
    addEvent(2, tr("Modem offline geschaltet (vom Catcher getrennt)"));
}

void CellMonitor::goOnline()
{
    ofonoSetProperty(QStringLiteral("org.ofono.Modem"),
                     QStringLiteral("Online"), true);
    addEvent(0, tr("Modem wieder online"));
}

// ---------------------------------------------------------------------------
// Whitelist of known-good cells, persisted with QSettings.
// ---------------------------------------------------------------------------
QString CellMonitor::cellKey() const
{
    return QStringLiteral("%1-%2-%3-%4")
            .arg(m_mcc, m_mnc).arg(m_lac).arg(m_cellId);
}

bool CellMonitor::currentCellTrusted() const
{
    return m_cellId != 0 && m_whitelist.contains(cellKey());
}

void CellMonitor::loadWhitelist()
{
    QSettings s;
    m_whitelist = QSet<QString>::fromList(
                s.value(QStringLiteral("whitelist")).toStringList());
}

void CellMonitor::saveWhitelist()
{
    QSettings s;
    s.setValue(QStringLiteral("whitelist"), QStringList(m_whitelist.toList()));
}

QStringList CellMonitor::whitelistKeys() const
{
    QStringList keys = m_whitelist.toList();
    keys.sort();
    return keys;
}

void CellMonitor::trustCurrentCell()
{
    if (m_cellId == 0) return;
    m_whitelist.insert(cellKey());
    saveWhitelist();
    addEvent(0, tr("Zelle als vertrauenswürdig markiert: %1").arg(cellKey()));
    emit cellChanged();
    emit whitelistChanged();
}

void CellMonitor::untrustCurrentCell()
{
    if (m_cellId == 0) return;
    m_whitelist.remove(cellKey());
    saveWhitelist();
    addEvent(0, tr("Zelle aus Whitelist entfernt: %1").arg(cellKey()));
    emit cellChanged();
    emit whitelistChanged();
}

void CellMonitor::removeFromWhitelist(const QString &key)
{
    if (!m_whitelist.remove(key)) return;
    saveWhitelist();
    addEvent(0, tr("Zelle aus Whitelist entfernt: %1").arg(key));
    emit cellChanged();
    emit whitelistChanged();
}

// ---------------------------------------------------------------------------
// Map samples + transmitter-location estimate.
//
// We can't triangulate a cell from one spot; instead we log (position, signal)
// as the user moves and estimate the source as the signal-weighted centroid of
// the samples — biased toward where the cell is strongest, i.e. nearest the
// transmitter. It's a rough first guess: the user has to move around, and it
// degrades on directional antennas. The estimate resets when the cell changes.
// ---------------------------------------------------------------------------
void CellMonitor::addSample()
{
    if (m_cellId != m_sampleCellId) {
        // new cell -> start a fresh sample set
        m_sampleCellId = m_cellId;
        m_samples.clear();
    }

    // signal metric: prefer RSRP (0..97 in ofono units), else strength %.
    const int sig = m_rsrp >= 0 ? m_rsrp : m_strength;

    // de-dupe near-identical fixes to avoid piling up while standing still
    if (!m_samples.isEmpty()) {
        const QVariantMap last = m_samples.last().toMap();
        if (qAbs(last.value(QStringLiteral("lat")).toDouble() - m_lat) < 1e-5 &&
            qAbs(last.value(QStringLiteral("lon")).toDouble() - m_lon) < 1e-5) {
            return;
        }
    }

    QVariantMap s;
    s.insert(QStringLiteral("lat"), m_lat);
    s.insert(QStringLiteral("lon"), m_lon);
    s.insert(QStringLiteral("sig"), sig);
    s.insert(QStringLiteral("cid"), m_cellId);
    m_samples.append(s);
    while (m_samples.size() > 500)
        m_samples.removeFirst();
    emit samplesChanged();

    recomputeEstimate();
}

void CellMonitor::recomputeEstimate()
{
    // signal-weighted centroid of the samples + their spatial spread
    double sumW = 0.0, sumLat = 0.0, sumLon = 0.0;
    double laMin = 90.0, laMax = -90.0, loMin = 180.0, loMax = -180.0;
    for (const QVariant &v : m_samples) {
        const QVariantMap s = v.toMap();
        const double la = s.value(QStringLiteral("lat")).toDouble();
        const double lo = s.value(QStringLiteral("lon")).toDouble();
        // weight strong signal much more heavily (closer to the transmitter)
        const double sig = s.value(QStringLiteral("sig")).toDouble();
        const double w = sig > 0 ? sig * sig : 0.0;
        sumW += w;
        sumLat += w * la;
        sumLon += w * lo;
        laMin = qMin(laMin, la); laMax = qMax(laMax, la);
        loMin = qMin(loMin, lo); loMax = qMax(loMax, lo);
    }

    // Prefer snapping the estimate to the nearest official BNetzA mast — but ONLY
    // when the cell isn't suspicious (a catcher doesn't sit on a licensed site, so
    // snapping there would hide the anomaly). Needs authority data loaded here and
    // a site within a plausible serving range. This is the likeliest transmitter,
    // far better than the centroid (which is stuck wherever we happened to stand).
    bool official = false;
    double eLat = 0.0, eLon = 0.0;
    if (m_threatLevel == 0 && m_cellId != m_ghostFlaggedCell &&
        m_bnetza && m_gpsValid &&
        m_lat != 0.0 && m_lon != 0.0 && m_bnetza->areaLoaded(m_lat, m_lon)) {
        const QVariantMap site = m_bnetza->nearestSite(m_lat, m_lon);
        if (!site.isEmpty() &&
            site.value(QStringLiteral("dist")).toDouble() <= 3000.0) {
            eLat = site.value(QStringLiteral("lat")).toDouble();
            eLon = site.value(QStringLiteral("lon")).toDouble();
            official = true;
        }
    }

    bool ok = false;
    if (official) {
        ok = true;
    } else if (sumW > 0.0 && m_samples.size() >= 3) {
        // fallback: weighted centroid, but only once the samples span enough
        // ground (~200 m) — otherwise the estimate sits right on top of you.
        const double cosLat = qCos(qDegreesToRadians((laMin + laMax) / 2.0));
        const double spanN = (laMax - laMin) * 111320.0;
        const double spanE = (loMax - loMin) * 111320.0 * cosLat;
        if (qSqrt(spanN * spanN + spanE * spanE) >= 200.0) {
            eLat = sumLat / sumW;
            eLon = sumLon / sumW;
            ok = true;
        }
    }

    // The estimate is untrustworthy as a benign tower when the cell raised a
    // threat or is ghost-flagged. Drives the red map marker; note snapping is
    // already suppressed in both cases, so a suspect estimate is our centroid.
    const bool suspect = (m_threatLevel > 0) ||
                         (m_cellId != 0 && m_cellId == m_ghostFlaggedCell);

    if (ok) {
        m_estLat = eLat;
        m_estLon = eLon;
    }
    if (ok != m_estValid || official != m_estOfficial ||
        suspect != m_estSuspect || ok) {
        m_estValid = ok;
        m_estOfficial = official;
        m_estSuspect = suspect;
        emit estChanged();
    }
}

void CellMonitor::clearSamples()
{
    m_samples.clear();
    m_sampleCellId = 0;
    m_estValid = false;
    m_estOfficial = false;
    emit samplesChanged();
    emit estChanged();
}

// ---------------------------------------------------------------------------
// SMS events (fed by the Tier 2 /dev/diag collector). Every event is enriched
// with time + GPS + serving cell, kept in memory for the UI, and appended to
// an NDJSON log immediately (persisted). CSV export on demand.
// ---------------------------------------------------------------------------
void CellMonitor::setTier2Active(bool on)
{
    if (on == m_tier2Active) return;
    m_tier2Active = on;
    emit tier2Changed();
}

// ---------------------------------------------------------------------------
// Tier 2 collector — run the /dev/diag helper as root through pkexec and turn
// its JSON alarm lines into SMS events / monitor threats.
//
// The polkit action (catchercatcher.diag) grants allow_active=yes,
// so from the user's own GUI session this needs no password.
//
// Stopping is subtle: the helper runs as root, but the app is defaultuser, which
// CANNOT signal a root process (kill -> EPERM). So we cannot terminate() pkexec.
// Instead the app drops a sentinel file (both can access /tmp) that the helper
// polls and then exits; it also exits on our-PID-death as a safety net.
// ---------------------------------------------------------------------------
static QString diagStopSentinel()
{
    return QStringLiteral("/tmp/harbour-catchercatcher-diag-stop.%1")
            .arg(QCoreApplication::applicationPid());
}

// Which root collector fits this baseband. Qualcomm reads /dev/diag; MediaTek has
// no /dev/diag and needs the DHL collector (CCCI/DHL via emdlogger). Both speak
// the same NDJSON protocol, take "service <pid>", and honour the same stop
// sentinel, so the rest of the Tier 2 code is platform-agnostic. /dev/diag wins
// if somehow both exist (it is the path Tier 2 was first built on).
static QString tier2HelperPath()
{
    if (QFileInfo::exists(QStringLiteral("/dev/diag")))
        return QStringLiteral("/usr/bin/harbour-catchercatcher-diag-helper");
    if (QFileInfo::exists(QStringLiteral("/dev/ccci_ccb_dhl")))
        return QStringLiteral("/usr/bin/harbour-catchercatcher-mtk-helper");
    return QString();
}

void CellMonitor::startTier2()
{
    if (m_diagProc) return;   // already running / starting
    const QString helper = tier2HelperPath();
    if (helper.isEmpty()) {
        addEvent(2, tr("Tier 2 nicht verfügbar: kein bekannter Baseband-"
                       "Diagnosekanal (weder Qualcomm /dev/diag noch MediaTek DHL)."));
        return;
    }
    m_diagBuf.clear();
    QFile::remove(diagStopSentinel());   // clear any stale stop request
    m_diagProc = new QProcess(this);
    connect(m_diagProc, SIGNAL(readyReadStandardOutput()),
            this, SLOT(onDiagReadyRead()));
    connect(m_diagProc, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(onDiagFinished(int, QProcess::ExitStatus)));
    connect(m_diagProc, SIGNAL(errorOccurred(QProcess::ProcessError)),
            this, SLOT(onDiagError(QProcess::ProcessError)));

    addEvent(0, tr("Tier 2 wird gestartet (ggf. Authentifizierung nötig) …"));
    // Pass our PID so the root helper reliably exits if the app goes away
    // (swipe-close/crash) even while the orphaned pkexec lingers.
    m_diagProc->start(QStringLiteral("pkexec"),
                      QStringList{
                          helper,
                          QStringLiteral("service"),
                          QString::number(QCoreApplication::applicationPid()) });
}

void CellMonitor::stopTier2()
{
    if (!m_diagProc) return;
    addEvent(0, tr("Tier 2 wird gestoppt …"));
    // We can't kill the root helper directly (EPERM). Drop the stop sentinel;
    // the helper polls it (~0.5 s), exits, pkexec follows, onDiagFinished fires.
    // Create it with O_NOFOLLOW: /tmp is world-writable on Sailfish, so without
    // it a planted symlink at this path could redirect the write to another file
    // (the helper that acts on the sentinel runs as root).
    const QByteArray sp = QFile::encodeName(diagStopSentinel());
    int fd = ::open(sp.constData(),
                    O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd >= 0)
        ::close(fd);
    // Best-effort SIGTERM too (a no-op EPERM here, but harmless).
    m_diagProc->terminate();
}

void CellMonitor::onDiagReadyRead()
{
    if (!m_diagProc) return;
    m_diagBuf += m_diagProc->readAllStandardOutput();
    int nl;
    while ((nl = m_diagBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_diagBuf.left(nl).trimmed();
        m_diagBuf.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        const QJsonDocument d = QJsonDocument::fromJson(line);
        if (!d.isObject())
            continue;
        const QVariantMap o = d.object().toVariantMap();
        const QString ev = o.value(QStringLiteral("ev")).toString();
        if (ev == QLatin1String("ready")) {
            setTier2Active(true);
            addEvent(0, tr("Tier 2 aktiv — Baseband-Überwachung läuft."));
        } else if (ev == QLatin1String("alarm")) {
            // copy the fields the SMS view understands; addSmsEvent enriches
            // with time/cell/GPS, persists, and raises a monitor threat.
            static const char *const keys[] = {
                "type", "severity", "from", "smsc", "pid", "dcs", "text",
                "kind", "l3", nullptr };
            QVariantMap sms;
            for (int i = 0; keys[i]; ++i) {
                const QString k = QString::fromLatin1(keys[i]);
                if (o.contains(k))
                    sms.insert(k, o.value(k));
            }
            addSmsEvent(sms);
        } else if (ev == QLatin1String("error")) {
            addEvent(2, tr("Tier-2-Fehler: %1")
                     .arg(o.value(QStringLiteral("msg")).toString()));
        }
    }
    // Defensive: a well-behaved helper emits newline-terminated JSON, so any
    // leftover is a partial line. Bound it so a malfunctioning stream can't
    // grow memory without limit.
    if (m_diagBuf.size() > 65536)
        m_diagBuf.clear();
}

void CellMonitor::onDiagFinished(int exitCode, QProcess::ExitStatus)
{
    setTier2Active(false);
    // pkexec: 126 = not authorised, 127 = auth dialog dismissed/failed
    if (exitCode == 126 || exitCode == 127)
        addEvent(1, tr("Tier 2: Authentifizierung abgebrochen oder verweigert."));
    else
        addEvent(0, tr("Tier 2 beendet."));
    if (m_diagProc) {
        m_diagProc->deleteLater();
        m_diagProc = nullptr;
    }
    m_diagBuf.clear();
    // tidy up the stop sentinel in case the helper exited another way
    QFile::remove(diagStopSentinel());
}

void CellMonitor::onDiagError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;   // other errors arrive with finished(); handled there
    addEvent(2, tr("Tier 2 konnte nicht gestartet werden (pkexec nicht verfügbar?)."));
    setTier2Active(false);
    if (m_diagProc) {
        m_diagProc->deleteLater();
        m_diagProc = nullptr;
    }
    m_diagBuf.clear();
}

void CellMonitor::addSmsEvent(const QVariantMap &ev)
{
    QVariantMap e = ev;
    e.insert(QStringLiteral("ts"),
             QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!e.contains(QStringLiteral("severity")))
        e.insert(QStringLiteral("severity"), 1);
    e.insert(QStringLiteral("cell"), cellKey());
    e.insert(QStringLiteral("lat"), m_lat);
    e.insert(QStringLiteral("lon"), m_lon);
    e.insert(QStringLiteral("_id"), ++m_smsSeq);   // stable id for row deletion

    m_smsEvents.prepend(e);
    while (m_smsEvents.size() > 500)
        m_smsEvents.removeLast();
    emit smsEventsChanged();

    // persist immediately (NDJSON, one line per event)
    rotateIfLarge(m_smsLogPath, 2 * 1024 * 1024);   // cap the SMS log at ~2 MB
    QFile f(m_smsLogPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << "{"
           << "\"ts\":\"" << e.value(QStringLiteral("ts")).toString() << "\","
           << "\"type\":\"" << jsonEscape(e.value(QStringLiteral("type")).toString()) << "\","
           << "\"severity\":" << e.value(QStringLiteral("severity")).toInt() << ","
           << "\"from\":\"" << jsonEscape(e.value(QStringLiteral("from")).toString()) << "\","
           << "\"sent\":\"" << jsonEscape(e.value(QStringLiteral("sent")).toString()) << "\","
           << "\"smsc\":\"" << jsonEscape(e.value(QStringLiteral("smsc")).toString()) << "\","
           << "\"pid\":\"" << jsonEscape(e.value(QStringLiteral("pid")).toString()) << "\","
           << "\"dcs\":\"" << jsonEscape(e.value(QStringLiteral("dcs")).toString()) << "\","
           << "\"cell\":\"" << e.value(QStringLiteral("cell")).toString() << "\","
           << "\"lat\":" << QString::number(m_lat, 'f', 6) << ","
           << "\"lon\":" << QString::number(m_lon, 'f', 6) << ","
           << "\"text\":\"" << jsonEscape(e.value(QStringLiteral("text")).toString()) << "\"}"
           << "\n";
        f.close();
    }

    // a suspicious SMS is also a monitor-level event/threat
    const int sev = e.value(QStringLiteral("severity")).toInt();
    if (sev >= 1)
        addEvent(sev, tr("SMS: %1").arg(e.value(QStringLiteral("type")).toString()));
}

void CellMonitor::clearSmsEvents()
{
    m_smsEvents.clear();
    emit smsEventsChanged();
    // also drop the persisted log so a restart doesn't reload cleared events
    QFile::remove(m_smsLogPath);
}

void CellMonitor::removeSmsEventById(qint64 id)
{
    for (int i = 0; i < m_smsEvents.size(); ++i) {
        if (m_smsEvents.at(i).toMap().value(QStringLiteral("_id")).toLongLong() == id) {
            removeSmsEventAt(i);
            return;
        }
    }
    // already gone (e.g. a stacked remorse fired twice) — nothing to do.
}

void CellMonitor::removeSmsEventAt(int index)
{
    if (index < 0 || index >= m_smsEvents.size()) return;
    m_smsEvents.removeAt(index);
    emit smsEventsChanged();

    // rewrite the NDJSON log from the surviving events (oldest last in the list,
    // so iterate in reverse to keep file order oldest-first).
    QFile f(m_smsLogPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream ts(&f);
    for (int i = m_smsEvents.size() - 1; i >= 0; --i) {
        const QVariantMap e = m_smsEvents.at(i).toMap();
        ts << "{"
           << "\"ts\":\"" << e.value(QStringLiteral("ts")).toString() << "\","
           << "\"type\":\"" << jsonEscape(e.value(QStringLiteral("type")).toString()) << "\","
           << "\"severity\":" << e.value(QStringLiteral("severity")).toInt() << ","
           << "\"from\":\"" << jsonEscape(e.value(QStringLiteral("from")).toString()) << "\","
           << "\"sent\":\"" << jsonEscape(e.value(QStringLiteral("sent")).toString()) << "\","
           << "\"smsc\":\"" << jsonEscape(e.value(QStringLiteral("smsc")).toString()) << "\","
           << "\"pid\":\"" << jsonEscape(e.value(QStringLiteral("pid")).toString()) << "\","
           << "\"dcs\":\"" << jsonEscape(e.value(QStringLiteral("dcs")).toString()) << "\","
           << "\"cell\":\"" << e.value(QStringLiteral("cell")).toString() << "\","
           << "\"lat\":" << QString::number(e.value(QStringLiteral("lat")).toDouble(), 'f', 6) << ","
           << "\"lon\":" << QString::number(e.value(QStringLiteral("lon")).toDouble(), 'f', 6) << ","
           << "\"text\":\"" << jsonEscape(e.value(QStringLiteral("text")).toString()) << "\"}"
           << "\n";
    }
    f.close();
}

void CellMonitor::loadSmsEvents()
{
    QFile f(m_smsLogPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QVariantList loaded;   // in file order (oldest first)
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument d = QJsonDocument::fromJson(line);
        if (d.isObject())
            loaded.append(d.object().toVariantMap());
    }
    f.close();
    while (loaded.size() > 500)
        loaded.removeFirst();
    // newest first for the UI, each tagged with a stable session id
    m_smsEvents.clear();
    for (const QVariant &v : loaded) {
        QVariantMap e = v.toMap();
        e.insert(QStringLiteral("_id"), ++m_smsSeq);
        m_smsEvents.prepend(e);
    }
    emit smsEventsChanged();
}

static QString csvField(const QString &s)
{
    QString v = s;
    v.replace('"', QStringLiteral("\"\""));
    return '"' + v + '"';
}

// ---------------------------------------------------------------------------
// Device identity: modem firmware + SIM (ICCID/IMSI). Baselined in QSettings so
// a change between runs (SIM swap or firmware flash) is flagged immediately.
// ---------------------------------------------------------------------------
void CellMonitor::readIdentity()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    QString fw, imei, iccid, imsi, op;

    QDBusInterface modem(OFONO, m_modemPath,
                         QStringLiteral("org.ofono.Modem"), bus);
    modem.setTimeout(1500);   // never block the GUI thread on a stalled ofono
    QDBusReply<QVariantMap> mr = modem.call(QStringLiteral("GetProperties"));
    if (mr.isValid()) {
        fw = mapStr(mr.value(), QStringLiteral("Revision"));
        imei = mapStr(mr.value(), QStringLiteral("Serial"));
    }
    QDBusInterface sim(OFONO, m_modemPath,
                       QStringLiteral("org.ofono.SimManager"), bus);
    sim.setTimeout(1500);
    QDBusReply<QVariantMap> sr = sim.call(QStringLiteral("GetProperties"));
    if (sr.isValid()) {
        iccid = mapStr(sr.value(), QStringLiteral("CardIdentifier"));
        imsi = mapStr(sr.value(), QStringLiteral("SubscriberIdentity"));
        op = mapStr(sr.value(), QStringLiteral("ServiceProviderName"));
    }

    if (fw.isEmpty() && iccid.isEmpty() && imsi.isEmpty())
        return;   // modem/SIM not ready yet

    const bool changed = (fw != m_firmwareVersion || imei != m_imei ||
                          iccid != m_simIccid || imsi != m_simImsi ||
                          op != m_simOperator);
    m_firmwareVersion = fw;
    m_imei = imei;
    m_simIccid = iccid;
    m_simImsi = imsi;
    m_simOperator = op;

    QSettings s;
    const QString bFw = s.value(QStringLiteral("baselineFirmware")).toString();
    const QString bIccid = s.value(QStringLiteral("baselineIccid")).toString();
    const QString bImsi = s.value(QStringLiteral("baselineImsi")).toString();

    if (!iccid.isEmpty()) {
        if (!bIccid.isEmpty() && bIccid != iccid) {
            m_simChanged = true;
            addEvent(3, tr("SIM gewechselt! ICCID %1 → %2").arg(bIccid, iccid));
        }
        s.setValue(QStringLiteral("baselineIccid"), iccid);
    }
    if (!imsi.isEmpty()) {
        if (!bImsi.isEmpty() && bImsi != imsi) {
            m_simChanged = true;
            addEvent(3, tr("SIM-IMSI geändert!"));
        }
        s.setValue(QStringLiteral("baselineImsi"), imsi);
    }
    if (!fw.isEmpty()) {
        if (!bFw.isEmpty() && bFw != fw) {
            m_firmwareChanged = true;
            addEvent(2, tr("Modem-Firmware geändert: %1 → %2").arg(bFw, fw));
        }
        s.setValue(QStringLiteral("baselineFirmware"), fw);
    }

    m_identityChecked = true;
    if (changed)
        emit identityChanged();
}

void CellMonitor::acknowledgeIdentity()
{
    if (!m_simChanged && !m_firmwareChanged) return;
    m_simChanged = false;
    m_firmwareChanged = false;
    emit identityChanged();
}

QString CellMonitor::exportSmsCsv()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/catchercatcher-sms-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
            + QStringLiteral(".csv");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        addEvent(1, tr("CSV-Export fehlgeschlagen: %1").arg(path));
        return QString();
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    // device identity as a leading comment block, then the column header
    ts << "# firmware," << csvField(m_firmwareVersion) << "\n";
    ts << "# imei," << csvField(m_imei) << "\n";
    ts << "# iccid," << csvField(m_simIccid) << "\n";
    ts << "# imsi," << csvField(m_simImsi) << "\n";
    ts << "# operator," << csvField(m_simOperator) << "\n";
    ts << "time,type,severity,from,smsc,pid,dcs,cell,lat,lon,text,fw_version,iccid,imsi\n";
    // oldest first for a readable export
    for (int i = m_smsEvents.size() - 1; i >= 0; --i) {
        const QVariantMap e = m_smsEvents.at(i).toMap();
        ts << csvField(e.value(QStringLiteral("ts")).toString()) << ','
           << csvField(e.value(QStringLiteral("type")).toString()) << ','
           << e.value(QStringLiteral("severity")).toInt() << ','
           << csvField(e.value(QStringLiteral("from")).toString()) << ','
           << csvField(e.value(QStringLiteral("smsc")).toString()) << ','
           << csvField(e.value(QStringLiteral("pid")).toString()) << ','
           << csvField(e.value(QStringLiteral("dcs")).toString()) << ','
           << csvField(e.value(QStringLiteral("cell")).toString()) << ','
           << e.value(QStringLiteral("lat")).toDouble() << ','
           << e.value(QStringLiteral("lon")).toDouble() << ','
           << csvField(e.value(QStringLiteral("text")).toString()) << ','
           << csvField(m_firmwareVersion) << ','
           << csvField(m_simIccid) << ','
           << csvField(m_simImsi) << '\n';
    }
    f.close();
    addEvent(0, tr("SMS-CSV exportiert: %1").arg(path));
    return path;
}
