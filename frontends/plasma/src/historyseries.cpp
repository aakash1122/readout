#include "historyseries.h"

#include <algorithm>

HistorySeries::HistorySeries(QObject *parent)
    : QObject(parent)
{
}

void HistorySeries::setSource(ReadoutSource *source)
{
    if (m_source == source) {
        return;
    }
    disconnect(m_connection);
    m_source = source;
    if (source != nullptr) {
        m_connection = connect(source, &ReadoutSource::historyChanged, this, [this](const QString &module) {
            if (module == m_module) {
                refresh();
            }
        });
    }
    Q_EMIT sourceChanged();
    refresh();
}

void HistorySeries::setModule(const QString &module)
{
    if (m_module == module) {
        return;
    }
    m_module = module;
    Q_EMIT moduleChanged();
    refresh();
}

void HistorySeries::setSeries(const QString &series)
{
    if (m_series == series) {
        return;
    }
    m_series = series;
    Q_EMIT seriesChanged();
    refresh();
}

void HistorySeries::setLength(int length)
{
    length = std::max(0, length);
    if (m_length == length) {
        return;
    }
    m_length = length;
    Q_EMIT lengthChanged();
    refresh();
}

void HistorySeries::refresh()
{
    QVariantList values;
    if (m_source != nullptr && !m_module.isEmpty() && !m_series.isEmpty()) {
        values = m_source->history(m_module, m_series);
        if (m_length > 0 && values.size() > m_length) {
            values = values.mid(values.size() - m_length);
        }
    }
    if (values.isEmpty() && m_values.isEmpty()) {
        return;
    }
    m_values = values;
    Q_EMIT valuesChanged();
}
