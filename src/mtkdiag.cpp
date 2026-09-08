/*
  harbour-catchercatcher — MediaTek baseband diagnostic surface
  Copyright (C) 2026  JimKnopfIoT — GPLv3 or later. See mtkdiag.h for the why.
*/
#include "mtkdiag.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVariantMap>

#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Node names. Qualcomm funnels everything through one character device;
// MediaTek splits it: ccb_dhl is the shared-memory log ring, raw_dhl the
// byte-stream fallback, ccb_ctrl the control channel that selects the filter.
// ---------------------------------------------------------------------------
static const char kQcomDiag[]   = "/dev/diag";
static const char kMtkCcbDhl[]  = "/dev/ccci_ccb_dhl";
static const char kMtkRawDhl[]  = "/dev/ccci_raw_dhl";
static const char kMtkCcbCtrl[] = "/dev/ccci_ccb_ctrl";
static const char kMtkLogCtrl[] = "/dev/ccci_md_log_ctrl";
// emdlogger's control surfaces: the abstract socket the vendor UI uses, and
// the FIFO pair that makes the same commands scriptable.
static const char kMtkSocket[]  = "@com.mediatek.mdlogger.socket1";
static const char kMtkCliFifo[] = "/data/mdlog/mdlog1_cli_fifo";
// The decoding database (DbgInfo_*.xz, MDDB_*.EDB) and the catcher filter blobs
// are embedded in the modem image, not installed into the rootfs.
static const char kModemA[]     = "/dev/block/by-name/modem_a";
static const char kModemB[]     = "/dev/block/by-name/modem_b";

MtkDiag::MtkDiag(QObject *parent)
    : QObject(parent)
{
    probe();
}

QString MtkDiag::platformName() const
{
    switch (m_platform) {
    case Qualcomm: return QStringLiteral("Qualcomm");
    case MediaTek: return QStringLiteral("MediaTek");
    default:       return tr("unbekannt");
    }
}

QString MtkDiag::readTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    // Device-tree properties are NUL-terminated; cut at the first NUL.
    QByteArray b = f.read(256);
    const int z = b.indexOf('\0');
    if (z >= 0)
        b.truncate(z);
    return QString::fromLatin1(b).trimmed();
}

int MtkDiag::findProcess(const QString &comm)
{
    QDir proc(QStringLiteral("/proc"));
    const QStringList entries =
            proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Unsorted);
    for (int i = 0; i < entries.size(); ++i) {
        bool numeric = false;
        const int pid = entries.at(i).toInt(&numeric);
        if (!numeric || pid <= 0)
            continue;
        if (readTrimmed(QStringLiteral("/proc/%1/comm").arg(pid)) == comm)
            return pid;
    }
    return -1;
}

bool MtkDiag::hasAbstractSocket(const QString &name)
{
    QFile f(QStringLiteral("/proc/net/unix"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    // readAll(), not a readLine()/atEnd() loop: procfs reports size 0, so
    // QFile::atEnd() is true straight away and such a loop reads nothing.
    // readAll() has the chunked fallback for zero-sized files.
    // Abstract sockets appear with a leading '@'. We only care that the name is
    // there at all — a bound-but-not-listening entry still proves the daemon.
    return QString::fromLatin1(f.readAll()).contains(name);
}

void MtkDiag::addFinding(const QString &label, bool ok, const QString &detail)
{
    QVariantMap m;
    m.insert(QStringLiteral("label"), label);
    m.insert(QStringLiteral("ok"), ok);
    m.insert(QStringLiteral("detail"), detail);
    m_findings.append(m);
}

void MtkDiag::probe()
{
    m_findings.clear();
    m_platform = UnknownPlatform;
    m_diagNode.clear();
    m_summary.clear();
    m_available = false;
    m_collector = false;
    m_needsRoot = false;
    m_ctrlSocket = false;
    m_ctrlFifo = false;
    m_decoderDb = false;
    m_loggerPid = -1;

    // SoC identification. soc0/machine is what MediaTek's platform driver
    // registers ("MT6858"); the device-tree model is the readable fallback.
    m_soc = readTrimmed(QStringLiteral("/sys/devices/soc0/machine"));
    if (m_soc.isEmpty())
        m_soc = readTrimmed(QStringLiteral("/proc/device-tree/model"));

    const bool qcom = QFileInfo(QString::fromLatin1(kQcomDiag)).exists();
    const bool mtk  = QFileInfo(QString::fromLatin1(kMtkCcbDhl)).exists()
                   || QFileInfo(QString::fromLatin1(kMtkRawDhl)).exists()
                   || m_soc.startsWith(QStringLiteral("MT"));

    // /dev/diag wins: it is the path Tier 2 is actually built and verified on.
    if (qcom)
        m_platform = Qualcomm;
    else if (mtk)
        m_platform = MediaTek;

    addFinding(tr("Plattform"), m_platform != UnknownPlatform,
               m_soc.isEmpty() ? platformName()
                               : QStringLiteral("%1 (%2)").arg(platformName(), m_soc));

    if (m_platform == Qualcomm)
        probeQualcomm();
    else if (m_platform == MediaTek)
        probeMediaTek();
    else
        m_summary = tr("Keine bekannte Baseband-Diagnose-Schnittstelle gefunden.");

    emit probed();
}

void MtkDiag::probeQualcomm()
{
    m_diagNode = QString::fromLatin1(kQcomDiag);
    m_available = true;
    m_collector = true;   // data/harbour-catchercatcher-diag-helper covers this
    m_needsRoot = (::access(kQcomDiag, R_OK) != 0);

    addFinding(tr("Diagnose-Knoten"), true, m_diagNode);
    addFinding(tr("Kollektor"), true, tr("diag-helper (Tier 2) vorhanden"));
    m_summary = tr("Qualcomm /dev/diag — Tier 2 wird unterstützt.");
}

void MtkDiag::probeMediaTek()
{
    // --- the log channels -------------------------------------------------
    // ccb_dhl is the one that matters: the shared-memory ring the modem writes
    // its log records into. raw_dhl and ccb_ctrl come with it.
    struct { const char *path; const char *what; } nodes[] = {
        { kMtkCcbDhl,  "ccb_dhl" },
        { kMtkRawDhl,  "raw_dhl" },
        { kMtkCcbCtrl, "ccb_ctrl" },
        { kMtkLogCtrl, "md_log_ctrl" },
    };
    QStringList present, missing;
    for (unsigned i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i) {
        if (QFileInfo(QString::fromLatin1(nodes[i].path)).exists())
            present << QString::fromLatin1(nodes[i].what);
        else
            missing << QString::fromLatin1(nodes[i].what);
    }
    const bool haveDhl = QFileInfo(QString::fromLatin1(kMtkCcbDhl)).exists();
    if (haveDhl) {
        m_diagNode = QString::fromLatin1(kMtkCcbDhl);
        m_available = true;
    }
    addFinding(tr("DHL-Kanäle"), haveDhl,
               missing.isEmpty() ? present.join(QStringLiteral(", "))
                                 : tr("vorhanden: %1 — fehlt: %2")
                                   .arg(present.join(QStringLiteral(", ")),
                                        missing.join(QStringLiteral(", "))));

    // The CCCI nodes are radio:radio 0660 — the app user cannot open them, so a
    // MediaTek collector needs the same pkexec route the Qualcomm one uses.
    m_needsRoot = haveDhl && (::access(kMtkCcbDhl, R_OK) != 0);
    if (haveDhl)
        addFinding(tr("Zugriff"), !m_needsRoot,
                   m_needsRoot ? tr("nur als root (Knoten gehört radio:radio)")
                               : tr("direkt lesbar"));

    // --- who owns the channel --------------------------------------------
    // emdlogger is the vendor daemon from the Android side. It holds ccb_dhl
    // open, so a collector must either drive it or take the channel over.
    m_loggerPid = findProcess(QStringLiteral("emdlogger"));
    addFinding(tr("emdlogger"), m_loggerPid > 0,
               m_loggerPid > 0 ? tr("läuft (PID %1), hält DHL offen").arg(m_loggerPid)
                               : tr("läuft nicht"));

    m_ctrlSocket = hasAbstractSocket(QString::fromLatin1(kMtkSocket));
    addFinding(tr("Steuer-Socket"), m_ctrlSocket,
               m_ctrlSocket ? QString::fromLatin1(kMtkSocket) : tr("nicht gebunden"));

    // The FIFO pair is the scriptable equivalent of the socket (mdlog_start,
    // full_filter, get_filter_info, …) and is the more practical entry point.
    struct stat st;
    m_ctrlFifo = (::stat(kMtkCliFifo, &st) == 0) && S_ISFIFO(st.st_mode);
    addFinding(tr("Steuer-FIFO"), m_ctrlFifo,
               m_ctrlFifo ? QString::fromLatin1(kMtkCliFifo) : tr("nicht vorhanden"));

    // --- decoding ---------------------------------------------------------
    // Without DbgInfo/MDDB the log records are opaque. Both are embedded in the
    // modem image together with the catcher_filter_*.bin profiles, so the
    // presence of the partition is what decides whether decoding is feasible.
    const QString modem = QFileInfo(QString::fromLatin1(kModemA)).exists()
            ? QString::fromLatin1(kModemA)
            : (QFileInfo(QString::fromLatin1(kModemB)).exists()
               ? QString::fromLatin1(kModemB) : QString());
    m_decoderDb = !modem.isEmpty();
    addFinding(tr("Decoder-Datenbank"), m_decoderDb,
               m_decoderDb ? tr("DbgInfo/MDDB im Modem-Image (%1)").arg(modem)
                           : tr("Modem-Partition nicht gefunden"));

    // --- collector --------------------------------------------------------
    // The MediaTek collector (harbour-catchercatcher-mtk-helper) drives emdlogger
    // over its control socket, tails the DHL MUZ log, and flags silent SMS
    // (Type-0 PID 0x40 and application-port pings) via an SCTS-anchored parser —
    // no MDDB needed. It ships with the app and needs the DHL channel + a way to
    // reach emdlogger (control socket or FIFO).
    const bool haveHelper =
            QFileInfo::exists(QStringLiteral("/usr/bin/harbour-catchercatcher-mtk-helper"));
    m_collector = m_available && haveHelper && (m_ctrlSocket || m_ctrlFifo);
    addFinding(tr("Kollektor"), m_collector,
               m_collector ? tr("MediaTek-DHL-Kollektor (stille SMS: Type-0 + Port-Ping)")
                           : (!haveHelper ? tr("Kollektor nicht installiert")
                              : tr("emdlogger-Steuerkanal fehlt")));

    if (!m_available)
        m_summary = tr("MediaTek erkannt, aber kein DHL-Kanal — Tier 2 nicht möglich.");
    else if (m_collector)
        m_summary = tr("MediaTek CCCI/DHL — Tier 2 (stille-SMS-Erkennung) wird unterstützt.");
    else
        m_summary = tr("MediaTek CCCI/DHL statt /dev/diag — Schnittstelle vorhanden, "
                       "Kollektor nicht einsatzbereit.");
}

QString MtkDiag::report() const
{
    QStringList out;
    out << m_summary;
    for (int i = 0; i < m_findings.size(); ++i) {
        const QVariantMap m = m_findings.at(i).toMap();
        out << QStringLiteral("%1 %2: %3")
               .arg(m.value(QStringLiteral("ok")).toBool()
                    ? QStringLiteral("+") : QStringLiteral("-"),
                    m.value(QStringLiteral("label")).toString(),
                    m.value(QStringLiteral("detail")).toString());
    }
    return out.join(QStringLiteral("\n"));
}
