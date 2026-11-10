#include "formatter.h"

#include <KLocalizedString>

#include <QLocale>

#include <cmath>

Formatter::Formatter(QObject *parent)
    : QObject(parent)
{
}

QString Formatter::bytes(double value, int precision) const
{
    return m_format.formatByteSize(value, precision);
}

QString Formatter::rate(double bytesPerSecond, bool bits) const
{
    const double value = bits ? bytesPerSecond * 8.0 : bytesPerSecond;
    const QString size = m_format.formatValue(value, bits ? KFormat::Unit::Bit : KFormat::Unit::Byte, 1, KFormat::UnitPrefix::AutoAdjust,
                                              KFormat::MetricBinaryDialect);
    return i18nc("@item:intext data rate, %1 is an amount like 1.2 MB", "%1/s", size);
}

QString Formatter::compactRate(double bytesPerSecond, bool bits) const
{
    double value = bits ? bytesPerSecond * 8.0 : bytesPerSecond;
    static const char *const prefixes[] = {"", "K", "M", "G", "T"};
    int index = 0;
    while (value >= 1000.0 && index < 4) {
        value /= 1000.0;
        ++index;
    }
    const int precision = value < 10.0 && index > 0 ? 1 : 0;
    const QString number = QLocale().toString(value, 'f', precision);
    const QString unit = QLatin1String(prefixes[index]) + (bits ? QStringLiteral("b") : QStringLiteral("B"));
    return number + unit;
}

QString Formatter::percent(double value, int precision) const
{
    return i18nc("@item:intext percentage", "%1%", QLocale().toString(value, 'f', precision));
}

QString Formatter::temperature(double celsius) const
{
    return i18nc("@item:intext temperature", "%1 °C", QLocale().toString(celsius, 'f', 0));
}

QString Formatter::frequency(double megahertz) const
{
    if (megahertz >= 1000.0) {
        return i18nc("@item:intext frequency", "%1 GHz", QLocale().toString(megahertz / 1000.0, 'f', 2));
    }
    return i18nc("@item:intext frequency", "%1 MHz", QLocale().toString(megahertz, 'f', 0));
}

QString Formatter::power(double watts) const
{
    return i18nc("@item:intext power", "%1 W", QLocale().toString(watts, 'f', watts < 10.0 ? 1 : 0));
}

QString Formatter::voltage(double volts) const
{
    return i18nc("@item:intext voltage", "%1 V", QLocale().toString(volts, 'f', 2));
}

QString Formatter::current(double amperes) const
{
    return i18nc("@item:intext electric current", "%1 A", QLocale().toString(amperes, 'f', 2));
}

QString Formatter::rpm(double value) const
{
    return i18nc("@item:intext fan speed", "%1 RPM", QLocale().toString(value, 'f', 0));
}

QString Formatter::duration(double seconds) const
{
    if (seconds <= 0 || !std::isfinite(seconds)) {
        return QString();
    }
    const auto milliseconds = static_cast<quint64>(seconds) * 1000U;
    // Drop seconds for anything longer than an hour.
    const quint64 rounded = seconds >= 3600 ? milliseconds - milliseconds % 60000U : milliseconds;
    return m_format.formatSpelloutDuration(rounded);
}

QString Formatter::number(double value, int precision) const
{
    return QLocale().toString(value, 'f', precision);
}

QString Formatter::sensor(double value, const QString &kind) const
{
    if (kind == QLatin1String("temperature")) {
        return temperature(value);
    }
    if (kind == QLatin1String("voltage")) {
        return voltage(value);
    }
    if (kind == QLatin1String("power")) {
        return power(value);
    }
    if (kind == QLatin1String("current")) {
        return current(value);
    }
    if (kind == QLatin1String("fan")) {
        return rpm(value);
    }
    return number(value, 1);
}
