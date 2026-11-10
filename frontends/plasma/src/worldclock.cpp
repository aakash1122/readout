#include "worldclock.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QLocale>
#include <QTimeZone>

WorldClock::WorldClock(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        Q_EMIT tick();
        schedule();
    });
    schedule();
}

void WorldClock::schedule()
{
    if (!m_active) {
        m_timer.stop();
        return;
    }
    // Wake exactly at the next second or minute boundary, never more often.
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 milliseconds = now.time().msec() + (m_showSeconds ? 0 : now.time().second() * 1000);
    const qint64 period = m_showSeconds ? 1000 : 60000;
    m_timer.start(static_cast<int>(period - milliseconds + 5));
}

void WorldClock::setZones(const QStringList &zones)
{
    if (m_zones == zones) {
        return;
    }
    m_zones = zones;
    Q_EMIT zonesChanged();
    Q_EMIT tick();
}

void WorldClock::setShowSeconds(bool showSeconds)
{
    if (m_showSeconds == showSeconds) {
        return;
    }
    m_showSeconds = showSeconds;
    Q_EMIT showSecondsChanged();
    Q_EMIT tick();
    schedule();
}

void WorldClock::setHourFormat(const QString &format)
{
    if (m_hourFormat == format) {
        return;
    }
    m_hourFormat = format;
    Q_EMIT hourFormatChanged();
    Q_EMIT tick();
}

void WorldClock::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged();
    if (active) {
        Q_EMIT tick();
    }
    schedule();
}

QString WorldClock::timeFormat() const
{
    QString format;
    if (m_hourFormat == QLatin1String("24")) {
        format = QStringLiteral("HH:mm");
    } else if (m_hourFormat == QLatin1String("12")) {
        format = QStringLiteral("h:mm AP");
    } else {
        format = QLocale().timeFormat(QLocale::ShortFormat);
    }
    if (m_showSeconds && !format.contains(QLatin1String("ss"))) {
        format.replace(QLatin1String("mm"), QLatin1String("mm:ss"));
    }
    return format;
}

QString WorldClock::time() const
{
    return QLocale().toString(QTime::currentTime(), timeFormat());
}

QString WorldClock::date() const
{
    return QLocale().toString(QDate::currentDate(), QLocale::LongFormat);
}

QString WorldClock::cityName(const QString &zone)
{
    QString city = zone.section(QLatin1Char('/'), -1);
    city.replace(QLatin1Char('_'), QLatin1Char(' '));
    return city;
}

QVariantMap WorldClock::entry(const QDateTime &zoned, const QDateTime &now, const QString &zoneId, const QString &city) const
{
    const int offsetMinutes = (zoned.offsetFromUtc() - now.offsetFromUtc()) / 60;
    QString offset;
    if (offsetMinutes == 0) {
        offset = i18nc("@info:status same time zone offset", "Same time");
    } else {
        const QString amount = offsetMinutes % 60 == 0 ? QLocale().toString(offsetMinutes / 60)
                                                        : QLocale().toString(offsetMinutes / 60.0, 'f', 1);
        offset = offsetMinutes > 0 ? i18nc("@info hours ahead", "+%1 h", amount) : i18nc("@info hours behind", "%1 h", amount);
    }
    const QTime time = zoned.time();
    return QVariantMap{
        {QStringLiteral("zone"), zoneId},
        {QStringLiteral("city"), city},
        {QStringLiteral("time"), QLocale().toString(time, timeFormat())},
        {QStringLiteral("date"), QLocale().toString(zoned.date(), QLocale::ShortFormat)},
        {QStringLiteral("offset"), offset},
        {QStringLiteral("hour"), time.hour()},
        {QStringLiteral("minute"), time.minute()},
        {QStringLiteral("second"), time.second()},
    };
}

QVariantMap WorldClock::local() const
{
    const QDateTime now = QDateTime::currentDateTime();
    return entry(now, now, QString::fromUtf8(QTimeZone::systemTimeZoneId()), i18nc("@title clock card", "Local time"));
}

QVariantList WorldClock::entries() const
{
    QVariantList list;
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &zoneId : m_zones) {
        const QTimeZone zone(zoneId.toUtf8());
        if (!zone.isValid()) {
            continue;
        }
        list.append(entry(now.toTimeZone(zone), now, zoneId, cityName(zoneId)));
    }
    return list;
}

QStringList WorldClock::availableZones() const
{
    QStringList zones;
    const auto ids = QTimeZone::availableTimeZoneIds();
    zones.reserve(ids.size());
    for (const QByteArray &id : ids) {
        if (id.contains('/') && !id.startsWith("Etc/")) {
            zones.append(QString::fromUtf8(id));
        }
    }
    return zones;
}
