/*
  harbour-catchercatcher — MediaTek baseband diagnostic surface
  Copyright (C) 2026  JimKnopfIoT

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version. This program is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License <https://www.gnu.org/licenses/> for details.

  ---------------------------------------------------------------------------
  Why this class exists
  ---------------------------------------------------------------------------
  Tier 2 was built against Qualcomm's /dev/diag (see docs/TIER2-DIAG-ABI.md).
  That node does NOT exist on MediaTek. The Jolla Phone (jp2601, MT6858,
  MOLY.NR17 modem) exposes the equivalent through MediaTek's CCCI stack:

      /dev/diag              (Qualcomm)   ->  /dev/ccci_ccb_dhl   (MediaTek)
                                              /dev/ccci_raw_dhl
                                              /dev/ccci_ccb_ctrl

  DHL is MediaTek's modem log channel; `emdlogger` (the vendor daemon from the
  Android side) owns it and is driven either through the abstract unix socket
  @com.mediatek.mdlogger.socket1 or the FIFO pair /data/mdlog/mdlog1_{cli,serv}_fifo.
  What the modem emits is selected by a "catcher filter" blob; the decoding
  database (DbgInfo/MDDB) is embedded in the modem partition, not shipped in
  the rootfs.

  This class does NOT collect logs. It answers the prerequisite question for
  any MediaTek Tier 2: which baseband platform is this, is the diagnostic
  surface present, and which of its pieces are actually usable here. The
  collector — the MediaTek counterpart of the /dev/diag helper — is a separate
  piece of work and is reported as missing until it exists.

  All probes are cheap and read-only: existence/permission checks on device
  nodes plus /proc scans. Nothing is opened that could disturb the modem.
*/
#ifndef MTKDIAG_H
#define MTKDIAG_H

#include <QObject>
#include <QString>
#include <QVariantList>

class MtkDiag : public QObject
{
    Q_OBJECT
    // Which baseband family this device has — decides which Tier 2 path applies.
    Q_PROPERTY(Platform platform READ platform NOTIFY probed)
    Q_PROPERTY(QString platformName READ platformName NOTIFY probed)
    Q_PROPERTY(QString socModel READ socModel NOTIFY probed)
    // The diagnostic node this platform uses ("/dev/diag" resp. the DHL channel).
    Q_PROPERTY(QString diagNode READ diagNode NOTIFY probed)
    // A diagnostic surface for this platform exists on the device …
    Q_PROPERTY(bool available READ available NOTIFY probed)
    // … and the app ships a collector able to read it. False on MediaTek for now.
    Q_PROPERTY(bool collectorAvailable READ collectorAvailable NOTIFY probed)
    // The nodes are radio:radio 0660 — reading them needs root either way.
    Q_PROPERTY(bool needsRoot READ needsRoot NOTIFY probed)
    // MediaTek specifics (all false on a Qualcomm device).
    Q_PROPERTY(bool loggerRunning READ loggerRunning NOTIFY probed)
    Q_PROPERTY(int loggerPid READ loggerPid NOTIFY probed)
    Q_PROPERTY(bool controlSocket READ controlSocket NOTIFY probed)
    Q_PROPERTY(bool controlFifo READ controlFifo NOTIFY probed)
    Q_PROPERTY(bool decoderDbPresent READ decoderDbPresent NOTIFY probed)
    // One German line for the UI, plus a per-check list for a detail view.
    Q_PROPERTY(QString summary READ summary NOTIFY probed)
    Q_PROPERTY(QVariantList findings READ findings NOTIFY probed)

public:
    enum Platform {
        UnknownPlatform = 0,
        Qualcomm        = 1,   // /dev/diag — the path Tier 2 is built on
        MediaTek        = 2    // CCCI/DHL — this class's subject
    };
    Q_ENUMS(Platform)

    explicit MtkDiag(QObject *parent = nullptr);

    Platform platform() const { return m_platform; }
    QString platformName() const;
    QString socModel() const { return m_soc; }
    QString diagNode() const { return m_diagNode; }
    bool available() const { return m_available; }
    bool collectorAvailable() const { return m_collector; }
    bool needsRoot() const { return m_needsRoot; }
    bool loggerRunning() const { return m_loggerPid > 0; }
    int loggerPid() const { return m_loggerPid; }
    bool controlSocket() const { return m_ctrlSocket; }
    bool controlFifo() const { return m_ctrlFifo; }
    bool decoderDbPresent() const { return m_decoderDb; }
    QString summary() const { return m_summary; }
    QVariantList findings() const { return m_findings; }

    // Re-run the whole probe. Cheap enough to call from onStatusChanged.
    Q_INVOKABLE void probe();
    // Multi-line plain-text version of `findings`, for the event log / support.
    Q_INVOKABLE QString report() const;

signals:
    void probed();

private:
    void addFinding(const QString &label, bool ok, const QString &detail);
    void probeQualcomm();
    void probeMediaTek();
    // /proc/<pid>/comm scan; returns the pid of the first match, else -1.
    static int findProcess(const QString &comm);
    // True when the abstract socket name appears as a listener in /proc/net/unix.
    static bool hasAbstractSocket(const QString &name);
    static QString readTrimmed(const QString &path);

    Platform m_platform = UnknownPlatform;
    QString m_soc;
    QString m_diagNode;
    QString m_summary;
    QVariantList m_findings;
    bool m_available = false;
    bool m_collector = false;
    bool m_needsRoot = false;
    bool m_ctrlSocket = false;
    bool m_ctrlFifo = false;
    bool m_decoderDb = false;
    int m_loggerPid = -1;
};

#endif // MTKDIAG_H
