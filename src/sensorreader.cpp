/*
  harbour-ibt — orientation / heading sensor reader for Sailfish OS
  Copyright (C) JimKnopfIoT — GPLv3 or later.
*/
#include "sensorreader.h"

#include <QtMath>
#include <QDebug>

SensorReader::SensorReader(QObject *parent)
    : QObject(parent)
    , m_acc(new QAccelerometer(this))
    , m_gyro(new QGyroscope(this))
    , m_compass(new QCompass(this))
    , m_mag(new QMagnetometer(this))
    , m_timer(new QTimer(this))
    , m_heading(0.0)
    , m_headingOut(0.0)
    , m_rate(0.0)
    , m_gain(1.0)       // deg/s on this device
    , m_bias(0.0)
    , m_calibrating(false)
    , m_calibSum(0.0)
    , m_calibN(0)
    , m_northLocked(false)
    , m_active(false)
    , m_available(false)
    , m_disturbed(false)
    , m_disturbCount(0)
    , m_field(0.0)
    , m_source(QStringLiteral("idle"))
    , m_intervalMs(25)
    , m_lastMs(0)
{
    m_available = m_gyro->connectToBackend();
    m_acc->connectToBackend();
    const bool comp = m_compass->connectToBackend();
    const bool magok = m_mag->connectToBackend();
    qWarning() << "compass connectToBackend=" << comp << "mag=" << magok;
    m_timer->setInterval(m_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &SensorReader::onTick);
}

void SensorReader::setGain(double g)
{
    if (qFuzzyCompare(m_gain, g))
        return;
    m_gain = g;
    emit gainChanged();
}

void SensorReader::start()
{
    if (m_active)
        return;
    m_acc->start();
    const bool ok = m_gyro->start();
    m_compass->setDataRate(25);     // ask for ~25 Hz so the heading isn't laggy
    m_compass->start();
    m_mag->setDataRate(25);
    m_mag->start();
    m_clock.start();
    m_lastMs = 0;
    // (re)measure the gyro bias over the first ~1 s (phone assumed still)
    m_calibrating = true;
    m_calibSum = 0.0;
    m_calibN = 0;
    m_northLocked = false;   // snap to the compass on the first plausible reading
    m_timer->start();
    m_active = true;
    m_source = ok ? QStringLiteral("gyro (relative)") : QStringLiteral("unavailable");
    qWarning() << "sensor start: gyro.connected=" << m_available
               << "gyro.start=" << ok
               << "acc.connected=" << m_acc->isConnectedToBackend()
               << "acc.active=" << m_acc->isActive()
               << "gyro.active=" << m_gyro->isActive();
    emit activeChanged();
    emit sourceChanged();
}

void SensorReader::stop()
{
    if (!m_active)
        return;
    m_timer->stop();
    m_gyro->stop();
    m_acc->stop();
    m_compass->stop();
    m_mag->stop();
    m_active = false;
    emit activeChanged();
}

void SensorReader::resetHeading()
{
    // heading is absolute (compass) now; snap straight to the current reading.
    QCompassReading *c = m_compass->reading();
    if (c) {
        m_heading = c->azimuth();
        m_headingOut = m_heading;   // snap the smoothed value too
    }
    emit headingChanged();
}

void SensorReader::onTick()
{
    // Complementary filter: the GYRO gives instant, smooth short-term rotation
    // (responsive), the COMPASS provides the absolute, drift-free reference so
    // it stays locked to North. Best of both: snappy AND absolute.

    // --- fast: integrate the gyro yaw (rate around gravity) ---
    QGyroscopeReading *g = m_gyro->reading();
    QAccelerometerReading *a = m_acc->reading();
    if (g) {
        const double gx = g->x(), gy = g->y(), gz = g->z(); // deg/s
        double ax = 0, ay = 0, az = 0;
        if (a) { ax = a->x(); ay = a->y(); az = a->z(); }
        const double gn = std::sqrt(ax * ax + ay * ay + az * az);
        if (gn >= 0.1) {
            const double yaw = -(gx * ax + gy * ay + gz * az) / gn; // deg/s, CW+
            m_rate = yaw;
            m_heading += yaw * (m_intervalMs / 1000.0);
        }
    }

    // --- interference gate: is the magnetic field plausibly Earth's? ---
    // Earth's total field is ~25..65 µT (≈49 µT in Germany). Near a notebook,
    // speakers, a charger or rebar the local field is distorted, so |B| leaves
    // that band — then the compass azimuth is garbage and we must NOT follow it
    // (that was the "north drifts toward the notebook" bug). We coast on the gyro.
    QMagnetometerReading *mm = m_mag->reading();
    bool fieldPlausible = true;                 // if no magnetometer, don't gate
    if (mm) {
        const double bx = mm->x(), by = mm->y(), bz = mm->z();   // Tesla
        m_field = std::sqrt(bx * bx + by * by + bz * bz) * 1e6;  // µT
        fieldPlausible = (m_field > 20.0 && m_field < 75.0);
    }

    // --- slow: pull toward the absolute compass heading (no drift) ---
    // The compass is the drift-free North reference. We deliberately DO NOT gate
    // on calibrationLevel() here: on the Xperia 10 III (hybris) it is ALWAYS 0,
    // which used to keep this correction permanently off and left the heading
    // gyro-only — that was the "North is wrong / drifts" bug. Instead we trust the
    // compass whenever the magnetic field magnitude is plausibly Earth's
    // (fieldPlausible); when it isn't (nearby metal), we coast on the gyro.
    // (Re-enabled with the user's sign-off, 2026-07-08.)
    QCompassReading *c = m_compass->reading();
    const bool compassGood = c && fieldPlausible;
    if (compassGood) {
        const double az = c->azimuth();
        if (!m_northLocked) {
            // first plausible fix: snap straight to North instead of starting at
            // an arbitrary 0 and slowly crawling there.
            m_heading = az;
            m_headingOut = az;
            m_northLocked = true;
        } else {
            double err = az - m_heading;
            while (err > 180.0) err -= 360.0;
            while (err < -180.0) err += 360.0;
            // reject only a near-opposite single-tick flip; otherwise pull firmly
            // enough to cancel gyro drift (0.08).
            if (std::fabs(err) < 170.0)
                m_heading += 0.08 * err;
        }
        if (m_source != QStringLiteral("compass+gyro")) {
            m_source = QStringLiteral("compass+gyro");
            emit sourceChanged();
        }
    } else if (m_source != QStringLiteral("gyro (relative)")) {
        m_source = QStringLiteral("gyro (relative)");
        emit sourceChanged();
    }

    // --- "Kompass gestört"-WARNUNG (nur Anzeige) — bewusst vom calib entkoppelt
    // (das ist auf diesem Gerät immer 0 → warnte sonst permanent). Warnung nur bei
    // ECHTER, ANHALTENDER Feldstörung: |B| deutlich weg vom Erdfeld (~49 µT) für
    // ~3 s. Langsam hoch / schnell runter → Banner kommt spät, verschwindet zügig.
    const bool fieldBad = !(m_field > 20.0 && m_field < 80.0);
    m_disturbCount = fieldBad ? qMin(160, m_disturbCount + 1)
                              : qMax(0, m_disturbCount - 4);
    const bool dist = (m_disturbCount >= 120);   // ~3 s (40 Ticks/s), bevor gewarnt wird
    m_disturbed = dist;                          // Benachrichtigung via headingChanged

    // low-rate diagnostics so we can tune the thresholds against real hardware
    static int dbg = 0;
    if ((dbg++ % 40) == 0)
        qWarning() << "CCOMPASS heading" << int(m_heading)
                   << "azimuth" << (c ? int(c->azimuth()) : -1)
                   << "field(uT)" << int(m_field) << "good" << compassGood
                   << "locked" << m_northLocked << "src" << m_source;

    m_heading = std::fmod(m_heading, 360.0);
    if (m_heading < 0.0)
        m_heading += 360.0;

    // --- output low-pass: the map follows this SMOOTHED heading, so it glides
    // toward the target instead of snapping to every hand tremor. SMOOTH is the
    // "sensitivity": lower = calmer/slower turn (0.10 here vs the raw, unsmoothed
    // heading iBT/iWifi feed straight to the UI). Eases along the SHORTEST arc.
    const double SMOOTH = 0.10;
    double d = m_heading - m_headingOut;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    m_headingOut += SMOOTH * d;
    m_headingOut = std::fmod(m_headingOut, 360.0);
    if (m_headingOut < 0.0)
        m_headingOut += 360.0;
    emit headingChanged();
}
