/*
  harbour-ibt — orientation / heading sensor reader for Sailfish OS
  Copyright (C) JimKnopfIoT — GPLv3 or later.

  Absolute heading via a complementary filter: the GYRO (yaw rate projected onto
  gravity, tilt-independent) gives smooth short-term rotation, the COMPASS gives
  the drift-free absolute North reference. A magnetometer interference gate drops
  the compass when the local field isn't plausibly Earth's (~25..65 µT) — e.g.
  near a notebook/metal — so North doesn't chase the disturbance; it then coasts
  on the gyro. Integrated on a FIXED timer interval (hybris timestamps unreliable).
*/
#ifndef SENSORREADER_H
#define SENSORREADER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QtSensors/QAccelerometer>
#include <QtSensors/QGyroscope>
#include <QtSensors/QCompass>
#include <QtSensors/QMagnetometer>

class SensorReader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double heading READ heading NOTIFY headingChanged)
    Q_PROPERTY(double rate READ rate NOTIFY headingChanged)
    Q_PROPERTY(double gain READ gain WRITE setGain NOTIFY gainChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    // true when the magnetometer looks disturbed (nearby metal/electronics) and
    // the compass reference is being ignored — the heading then coasts on the gyro.
    Q_PROPERTY(bool disturbed READ disturbed NOTIFY headingChanged)
public:
    explicit SensorReader(QObject *parent = nullptr);

    double heading() const { return m_headingOut; }   // smoothed (display) heading
    double rate() const { return m_rate; }
    double gain() const { return m_gain; }
    void setGain(double g);
    bool active() const { return m_active; }
    QString source() const { return m_source; }
    bool available() const { return m_available; }
    bool disturbed() const { return m_disturbed; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void resetHeading();

signals:
    void headingChanged();
    void activeChanged();
    void sourceChanged();
    void gainChanged();

private slots:
    void onTick();

private:
    QAccelerometer *m_acc;
    QGyroscope *m_gyro;
    QCompass *m_compass;
    QMagnetometer *m_mag;
    QTimer *m_timer;
    double m_heading;     // fused heading, 0..360
    double m_headingOut;  // low-pass smoothed heading fed to the map (calmer turn)
    double m_rate;        // last yaw rate (deg/s)
    double m_gain;        // calibration multiplier
    double m_bias;        // estimated gyro zero-rate bias (deg/s)
    bool m_calibrating;   // measuring the bias at start (phone held still)
    double m_calibSum;
    int m_calibN;
    bool m_northLocked;   // have we snapped the heading to the compass once yet?
    bool m_active;
    bool m_available;
    bool m_disturbed;     // debounced UI warning: magnetic field looks disturbed
    int m_disturbCount;   // hysteresis counter for m_disturbed (anti-flicker)
    double m_field;       // last magnetometer magnitude (µT), for diagnostics
    QString m_source;
    int m_intervalMs;
    QElapsedTimer m_clock;
    qint64 m_lastMs;
};

#endif // SENSORREADER_H
