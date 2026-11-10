// QML singleton "Format": locale-aware value formatting for every widget and popup.
#pragma once

#include <KFormat>

#include <QObject>
#include <QQmlEngine>

class Formatter : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Format)
    QML_SINGLETON

public:
    explicit Formatter(QObject *parent = nullptr);

    // Sizes: binary units as configured in KDE (KiB or KB per the user's setting).
    Q_INVOKABLE QString bytes(double value, int precision = 1) const;
    // Speeds: decimal units, bytes or bits per second.
    Q_INVOKABLE QString rate(double bytesPerSecond, bool bits = false) const;
    // Compact speed for narrow panels: "1.2M", "640K".
    Q_INVOKABLE QString compactRate(double bytesPerSecond, bool bits = false) const;
    Q_INVOKABLE QString percent(double value, int precision = 0) const;
    Q_INVOKABLE QString temperature(double celsius) const;
    Q_INVOKABLE QString frequency(double megahertz) const;
    Q_INVOKABLE QString power(double watts) const;
    Q_INVOKABLE QString voltage(double volts) const;
    Q_INVOKABLE QString current(double amperes) const;
    Q_INVOKABLE QString rpm(double value) const;
    Q_INVOKABLE QString duration(double seconds) const;
    Q_INVOKABLE QString number(double value, int precision = 0) const;
    // Formats a sensor value by its kind ("temperature", "voltage", "power", "current", "fan").
    Q_INVOKABLE QString sensor(double value, const QString &kind) const;

private:
    KFormat m_format;
};
