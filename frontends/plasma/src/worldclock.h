// QML element: local time plus configured time zones, ticking once per second or minute.
// The timer runs only while `active` is true (panel showing a clock, or the popup open).
#pragma once

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class WorldClock : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList zones READ zones WRITE setZones NOTIFY zonesChanged)
    Q_PROPERTY(bool showSeconds READ showSeconds WRITE setShowSeconds NOTIFY showSecondsChanged)
    Q_PROPERTY(QString hourFormat READ hourFormat WRITE setHourFormat NOTIFY hourFormatChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // Local time and date, formatted.
    Q_PROPERTY(QString time READ time NOTIFY tick)
    Q_PROPERTY(QString date READ date NOTIFY tick)
    // The current local date and time, for calendars.
    Q_PROPERTY(QDateTime now READ now NOTIFY tick)
    // Local time as an entry: {zone, city, time, date, offset, hour, minute, second}
    Q_PROPERTY(QVariantMap local READ local NOTIFY tick)
    // One entry per zone, same keys as `local`. hour, minute and second are numbers for analog clocks.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY tick)

public:
    explicit WorldClock(QObject *parent = nullptr);

    QStringList zones() const { return m_zones; }
    void setZones(const QStringList &zones);
    bool showSeconds() const { return m_showSeconds; }
    void setShowSeconds(bool showSeconds);
    // "locale", "24" or "12"
    QString hourFormat() const { return m_hourFormat; }
    void setHourFormat(const QString &format);
    bool active() const { return m_active; }
    void setActive(bool active);

    QString time() const;
    QString date() const;
    QDateTime now() const { return QDateTime::currentDateTime(); }
    QVariantMap local() const;
    QVariantList entries() const;

    // All IANA zone ids, for the settings page.
    Q_INVOKABLE QStringList availableZones() const;
    static QString cityName(const QString &zone);

Q_SIGNALS:
    void zonesChanged();
    void showSecondsChanged();
    void hourFormatChanged();
    void activeChanged();
    void tick();

private:
    void schedule();
    QString timeFormat() const;
    QVariantMap entry(const QDateTime &zoned, const QDateTime &now, const QString &zoneId, const QString &city) const;

    QStringList m_zones;
    bool m_showSeconds = false;
    QString m_hourFormat = QStringLiteral("locale");
    bool m_active = true;
    QTimer m_timer;
};
