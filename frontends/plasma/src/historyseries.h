// QML element: one history series of a ReadoutSource, kept current as history arrives.
//
//   HistorySeries { id: usage; source: page.source; module: "cpu"; series: "usage" }
//   LineHistory { values: usage.values }
//
// A property with a change signal, so bindings that read `values` always re-evaluate,
// including bindings the QML compiler turns into C++.
#pragma once

#include "readoutsource.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>

class HistorySeries : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(ReadoutSource *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString module READ module WRITE setModule NOTIFY moduleChanged)
    // Series name, see ReadoutSource::history(). Empty means no values.
    Q_PROPERTY(QString series READ series WRITE setSeries NOTIFY seriesChanged)
    // Keep only the newest `length` values; 0 keeps all.
    Q_PROPERTY(int length READ length WRITE setLength NOTIFY lengthChanged)
    // Oldest first.
    Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged)

public:
    explicit HistorySeries(QObject *parent = nullptr);

    ReadoutSource *source() const { return m_source; }
    void setSource(ReadoutSource *source);
    QString module() const { return m_module; }
    void setModule(const QString &module);
    QString series() const { return m_series; }
    void setSeries(const QString &series);
    int length() const { return m_length; }
    void setLength(int length);
    QVariantList values() const { return m_values; }

Q_SIGNALS:
    void sourceChanged();
    void moduleChanged();
    void seriesChanged();
    void lengthChanged();
    void valuesChanged();

private:
    void refresh();

    QPointer<ReadoutSource> m_source;
    QMetaObject::Connection m_connection;
    QString m_module;
    QString m_series;
    int m_length = 0;
    QVariantList m_values;
};
