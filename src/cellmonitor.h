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

#ifndef CELLMONITOR_H
#define CELLMONITOR_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include <QTimer>
#include <QProcess>
#include <QByteArray>
#include <QDBusObjectPath>

class BnetzaLookup;

/*
 * CellMonitor — Tier 1 backend for harbour-catchercatcher.
 *
 * Reads the cellular state from ofono over the system D-Bus, no root needed:
 *   - org.ofono.NetworkRegistration   -> operator, technology, CellId, strength
 *   - org.ofono.NetworkMonitor        -> EARFCN, RSRP, RSRQ (serving cell)
 *   - org.nemomobile.ofono.CellInfo   -> neighbouring-cell count (Jolla ext.)
 *
 * It keeps a short history and runs behavioural heuristics that flag the
 * classic IMSI-catcher fingerprints observable at this (cooked) layer:
 * forced downgrade to 2G/3G, hopping to an unseen cell, a serving cell with
 * no visible neighbours. Radio-layer signals (A5/0 cipher downgrade, silent
 * SMS) live one layer below and are the job of the Tier 2 /dev/diag helper.
 */
class CellMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool monitoring READ monitoring NOTIFY monitoringChanged)

    Q_PROPERTY(QString technology READ technology NOTIFY cellChanged)
    Q_PROPERTY(QString operatorName READ operatorName NOTIFY cellChanged)
    Q_PROPERTY(QString mcc READ mcc NOTIFY cellChanged)
    Q_PROPERTY(QString mnc READ mnc NOTIFY cellChanged)
    Q_PROPERTY(uint cellId READ cellId NOTIFY cellChanged)
    Q_PROPERTY(int enbId READ enbId NOTIFY cellChanged)
    Q_PROPERTY(int lac READ lac NOTIFY cellChanged)
    Q_PROPERTY(int earfcn READ earfcn NOTIFY cellChanged)
    Q_PROPERTY(int rsrp READ rsrp NOTIFY cellChanged)
    Q_PROPERTY(int rsrq READ rsrq NOTIFY cellChanged)
    Q_PROPERTY(int strength READ strength NOTIFY cellChanged)
    Q_PROPERTY(int neighbourCount READ neighbourCount NOTIFY cellChanged)

    // 0 = clear, 1 = info, 2 = warning, 3 = alert
    Q_PROPERTY(int threatLevel READ threatLevel NOTIFY threatChanged)
    Q_PROPERTY(QString threatText READ threatText NOTIFY threatChanged)

    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)

    // SMS events surfaced by the Tier 2 /dev/diag layer (silent/binary/OTA SMS).
    // Empty until the diag collector is wired in.
    Q_PROPERTY(QVariantList smsEvents READ smsEvents NOTIFY smsEventsChanged)
    Q_PROPERTY(bool tier2Active READ tier2Active NOTIFY tier2Changed)

    // device identity: modem firmware + SIM. Baselined in QSettings so a change
    // between runs (SIM swap / firmware flash) is flagged on the next launch.
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY identityChanged)
    Q_PROPERTY(QString imei READ imei NOTIFY identityChanged)
    Q_PROPERTY(QString simIccid READ simIccid NOTIFY identityChanged)
    Q_PROPERTY(QString simImsi READ simImsi NOTIFY identityChanged)
    Q_PROPERTY(QString simOperator READ simOperator NOTIFY identityChanged)
    Q_PROPERTY(bool simChanged READ simChanged NOTIFY identityChanged)
    Q_PROPERTY(bool firmwareChanged READ firmwareChanged NOTIFY identityChanged)

    // logging + mitigation + whitelist
    Q_PROPERTY(bool logging READ logging NOTIFY settingsChanged)
    Q_PROPERTY(QString logPath READ logPath CONSTANT)
    Q_PROPERTY(bool autoProtect READ autoProtect NOTIFY settingsChanged)
    Q_PROPERTY(QString technologyPreference READ technologyPreference NOTIFY cellChanged)
    Q_PROPERTY(bool currentCellTrusted READ currentCellTrusted NOTIFY cellChanged)
    // last position handed in from QML (QtPositioning), for the log + map
    Q_PROPERTY(double lat READ lat WRITE setLat NOTIFY posChanged)
    Q_PROPERTY(double lon READ lon WRITE setLon NOTIFY posChanged)
    Q_PROPERTY(bool gpsEnabled READ gpsEnabled WRITE setGpsEnabled NOTIFY gpsChanged)
    Q_PROPERTY(bool gpsValid READ gpsValid WRITE setGpsValid NOTIFY gpsChanged)

    // persisted map-view preferences (survive page switches AND app restarts)
    Q_PROPERTY(bool mapStreets  READ mapStreets  WRITE setMapStreets  NOTIFY mapPrefsChanged)
    Q_PROPERTY(bool mapTowers   READ mapTowers   WRITE setMapTowers   NOTIFY mapPrefsChanged)
    Q_PROPERTY(bool mapLicensed READ mapLicensed WRITE setMapLicensed NOTIFY mapPrefsChanged)
    Q_PROPERTY(bool mapRotate   READ mapRotate   WRITE setMapRotate   NOTIFY mapPrefsChanged)
    // the whitelist as a sorted key list, for the editor page
    Q_PROPERTY(QStringList whitelist READ whitelistKeys NOTIFY whitelistChanged)

    // signal-strength samples of the *current* cell and the resulting
    // best-guess transmitter location (signal-weighted centroid).
    Q_PROPERTY(QVariantList samples READ samples NOTIFY samplesChanged)
    Q_PROPERTY(double estLat READ estLat NOTIFY estChanged)
    Q_PROPERTY(double estLon READ estLon NOTIFY estChanged)
    Q_PROPERTY(bool estValid READ estValid NOTIFY estChanged)
    // true when the estimate was snapped to the nearest official BNetzA site
    // (only for non-suspicious cells) rather than computed from our samples.
    Q_PROPERTY(bool estOfficial READ estOfficial NOTIFY estChanged)
    // true when the estimate must NOT be read as a benign tower: the serving
    // cell raised a threat (>=2) or is ghost-flagged (strong signal but no /
    // only a distant licensed BNetzA mast). The map draws it red, not amber.
    Q_PROPERTY(bool estSuspect READ estSuspect NOTIFY estChanged)

public:
    explicit CellMonitor(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool monitoring() const { return m_monitoring; }
    QString technology() const { return m_technology; }
    QString operatorName() const { return m_operatorName; }
    QString mcc() const { return m_mcc; }
    QString mnc() const { return m_mnc; }
    uint cellId() const { return m_cellId; }
    int enbId() const { return m_cellId ? int(m_cellId >> 8) : -1; }
    int lac() const { return m_lac; }
    int earfcn() const { return m_earfcn; }
    int rsrp() const { return m_rsrp; }
    int rsrq() const { return m_rsrq; }
    int strength() const { return m_strength; }
    int neighbourCount() const { return m_neighbourCount; }
    int threatLevel() const { return m_threatLevel; }
    QString threatText() const { return m_threatText; }
    QVariantList events() const { return m_events; }

    QVariantList smsEvents() const { return m_smsEvents; }
    bool tier2Active() const { return m_tier2Active; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    QString imei() const { return m_imei; }
    QString simIccid() const { return m_simIccid; }
    QString simImsi() const { return m_simImsi; }
    QString simOperator() const { return m_simOperator; }
    bool simChanged() const { return m_simChanged; }
    bool firmwareChanged() const { return m_firmwareChanged; }
    bool logging() const { return m_logging; }
    QString logPath() const { return m_logPath; }
    bool autoProtect() const { return m_autoProtect; }
    QString technologyPreference() const { return m_techPref; }
    bool currentCellTrusted() const;
    double lat() const { return m_lat; }
    double lon() const { return m_lon; }
    bool gpsEnabled() const { return m_gpsEnabled; }
    bool gpsValid() const { return m_gpsValid; }
    bool mapStreets() const { return m_mapStreets; }
    bool mapTowers() const { return m_mapTowers; }
    bool mapLicensed() const { return m_mapLicensed; }
    bool mapRotate() const { return m_mapRotate; }
    QStringList whitelistKeys() const;
    QVariantList samples() const { return m_samples; }
    double estLat() const { return m_estLat; }
    double estLon() const { return m_estLon; }
    bool estValid() const { return m_estValid; }
    bool estOfficial() const { return m_estOfficial; }
    bool estSuspect() const { return m_estSuspect; }

public slots:
    void start();
    void stop();
    void poll();
    void acknowledge();   // reset threat level back to clear
    void clearEvents();

    // logging
    void setLogging(bool on);

    // mitigation — steer the modem away from a catcher
    void setAutoProtect(bool on);
    void setTechnologyPreference(const QString &tech);  // "gsm"/"umts"/"lte"/"nr"
    void lockLteOnly();          // block 2G/3G downgrade catchers
    void allowAllRat();          // restore normal selection
    void goOffline();            // detach from the current (suspect) cell
    void goOnline();

    // whitelist (persisted): mark the current cell known-good / suspect
    void trustCurrentCell();
    void untrustCurrentCell();
    void removeFromWhitelist(const QString &key);   // for the whitelist editor

    // persisted map-view preferences
    void setMapStreets(bool v);
    void setMapTowers(bool v);
    void setMapLicensed(bool v);
    void setMapRotate(bool v);

    void clearSamples();   // drop collected map samples for the current cell

    // SMS events (fed by the Tier 2 diag collector; also usable for testing).
    // ev: { type, severity(0..3), from, smsc, pid, dcs, text }
    void addSmsEvent(const QVariantMap &ev);
    void clearSmsEvents();
    void removeSmsEventAt(int index);       // delete by (volatile) row index
    void removeSmsEventById(qint64 id);     // delete by stable per-session id
    void setTier2Active(bool on);
    QString exportSmsCsv();   // write a CSV to ~/Documents, return the path
    void acknowledgeIdentity();   // clear SIM/firmware-changed flags

    // Tier 2 — start/stop the /dev/diag collector (root, via pkexec/polkit).
    // Its JSON alarm lines are fed into addSmsEvent()/the event log.
    void startTier2();
    void stopTier2();

    // wire the BNetzA licensed-site source in for the Ghost-Site heuristic
    void setBnetzaSource(QObject *b);

    // position feed from QML
    void setLat(double v);
    void setLon(double v);
    void setGpsEnabled(bool v);
    void setGpsValid(bool v);

signals:
    void availableChanged();
    void monitoringChanged();
    void cellChanged();
    void threatChanged();
    void eventsChanged();
    void settingsChanged();
    void posChanged();
    void gpsChanged();
    void samplesChanged();
    void estChanged();
    void smsEventsChanged();
    void tier2Changed();
    void identityChanged();
    void mapPrefsChanged();
    void whitelistChanged();

private slots:
    // live incoming SMS from ofono MessageManager (metadata browsing + capture)
    void onIncomingMessage(const QString &message, const QVariantMap &info);
    void onImmediateMessage(const QString &message, const QVariantMap &info);

    // Tier 2 diag collector (QProcess running pkexec … diag-helper service)
    void onDiagReadyRead();
    void onDiagFinished(int exitCode, QProcess::ExitStatus status);
    void onDiagError(QProcess::ProcessError error);

    // voice-call state (ofono VoiceCallManager) — used to suppress the CSFB
    // downgrade that a CS voice call triggers when VoLTE/IMS is off.
    void onCallAdded(const QDBusObjectPath &path, const QVariantMap &props);
    void onCallRemoved(const QDBusObjectPath &path);

private:
    void addEvent(int level, const QString &text);
    void raiseThreat(int level, const QString &text);
    void writeLog(const QString &kind, const QString &text);
    void ofonoSetProperty(const QString &iface, const QString &prop,
                          const QVariant &value);
    bool inCsfbWindow() const;   // a CS voice call is active or just ended
    QString cellKey() const;     // "MCC-MNC-LAC-CID" of the current cell
    void loadWhitelist();
    void saveWhitelist();
    void addSample();            // capture a GPS+signal sample for the map
    void recomputeEstimate();    // signal-weighted centroid of the samples
    void readIdentity();         // modem firmware + SIM id, with change detection
    void loadSmsEvents();        // reload persisted SMS events on startup
    static int techRank(const QString &tech);   // gsm<umts<lte<nr

    QTimer m_timer;
    QString m_modemPath;
    bool m_available = false;
    bool m_monitoring = false;

    QString m_technology;
    QString m_operatorName;
    QString m_mcc;
    QString m_mnc;
    uint m_cellId = 0;
    int m_lac = -1;
    int m_earfcn = -1;
    int m_rsrp = -1;
    int m_rsrq = -1;
    int m_strength = -1;
    int m_neighbourCount = -1;

    int m_threatLevel = 0;
    QString m_threatText;
    QVariantList m_events;

    bool m_logging = false;
    QString m_logPath;
    bool m_autoProtect = false;
    QString m_techPref;
    double m_lat = 0.0;
    double m_lon = 0.0;
    bool m_gpsEnabled = false;
    bool m_gpsValid = false;
    QSet<QString> m_activeCalls;   // ofono voicecall paths currently up
    qint64 m_lastCallEndMs = 0;    // when the last call ended (CSFB grace window)
    bool m_mapStreets = false;
    bool m_mapTowers = false;
    bool m_mapLicensed = false;
    bool m_mapRotate = false;
    QSet<QString> m_whitelist;
    BnetzaLookup *m_bnetza = nullptr;   // licensed-site source (Ghost-Site check)
    uint m_ghostCheckedCell = 0;        // cid we last ran the ghost check on
    uint m_ghostFlaggedCell = 0;        // cid the ghost check flagged as suspect

    QVariantList m_samples;      // {lat,lon,rsrp,cid} for the current cell
    uint m_sampleCellId = 0;     // cid the current samples belong to
    double m_estLat = 0.0;
    double m_estLon = 0.0;
    bool m_estValid = false;
    bool m_estOfficial = false;   // estimate snapped to a BNetzA site
    bool m_estSuspect = false;    // estimate not trustworthy (threat or ghost)

    QVariantList m_smsEvents;
    qint64 m_smsSeq = 0;   // monotonic id assigned to each event this session
    bool m_tier2Active = false;
    QString m_smsLogPath;
    QProcess *m_diagProc = nullptr;   // Tier 2 collector process (pkexec helper)
    QByteArray m_diagBuf;             // stdout line-assembly buffer

    QString m_firmwareVersion;
    QString m_imei;
    QString m_simIccid;
    QString m_simImsi;
    QString m_simOperator;
    bool m_simChanged = false;
    bool m_firmwareChanged = false;
    bool m_identityChecked = false;
    int m_identityRecheck = 0;

    // heuristic history
    bool m_havePrev = false;
    QString m_prevTech;
    uint m_prevCellId = 0;
    int m_prevLac = -1;
    QSet<uint> m_seenCells;
    int m_noNeighbourStreak = 0;
};

#endif // CELLMONITOR_H
