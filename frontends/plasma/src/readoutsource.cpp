#include "readoutsource.h"

#include <algorithm>

ReadoutSource::ReadoutSource(QObject *parent)
    : QObject(parent)
{
    ReadoutClient *client = ReadoutClient::instance();
    connect(client, &ReadoutClient::moduleUpdated, this, [this](const QString &module) {
        if (!m_modules.contains(module)) {
            return;
        }
        m_data.insert(module, shownData(module));
        Q_EMIT dataChanged();
        Q_EMIT updated(module);
        updateDetailReady();
    });
    connect(client, &ReadoutClient::historyUpdated, this, [this](const QString &module) {
        if (m_modules.contains(module)) {
            Q_EMIT historyChanged(module);
        }
    });
    connect(client, &ReadoutClient::infoChanged, this, &ReadoutSource::infoChanged);
    connect(client, &ReadoutClient::statusChanged, this, &ReadoutSource::statusChanged);
}

ReadoutSource::~ReadoutSource()
{
    if (m_consumerId != 0) {
        ReadoutClient::instance()->removeConsumer(m_consumerId);
    }
}

void ReadoutSource::classBegin()
{
}

void ReadoutSource::componentComplete()
{
    m_complete = true;
    publish();
}

void ReadoutSource::publish()
{
    if (!m_complete) {
        return;
    }
    ReadoutClient *client = ReadoutClient::instance();
    if (!m_active || m_modules.isEmpty()) {
        if (m_consumerId != 0) {
            client->removeConsumer(m_consumerId);
            m_consumerId = 0;
        }
        return;
    }
    if (m_consumerId == 0) {
        m_consumerId = client->addConsumer();
    }
    ReadoutClient::Consumer consumer;
    for (const QString &module : std::as_const(m_modules)) {
        consumer.modules.insert(module, m_detail ? ReadoutClient::Tier::Detail : ReadoutClient::Tier::Panel);
    }
    consumer.sensors = m_sensors;
    consumer.intervalMilliseconds = m_interval;
    consumer.cores = m_cores;
    client->setConsumer(m_consumerId, consumer);

    // Seed with what the client already has, so a new widget shows values immediately.
    refreshData();
    for (const QString &module : std::as_const(m_modules)) {
        client->requestHistory(module);
    }
}

void ReadoutSource::setModules(const QStringList &modules)
{
    if (m_modules == modules) {
        return;
    }
    m_modules = modules;
    m_data.clear();
    Q_EMIT modulesChanged();
    Q_EMIT dataChanged();
    publish();
    updateDetailReady();
}

void ReadoutSource::setDetail(bool detail)
{
    if (m_detail == detail) {
        return;
    }
    m_detail = detail;
    Q_EMIT detailChanged();
    publish(); // also refreshes the shown data, with the cached detail payload while opening
    updateDetailReady();
    if (detail) {
        for (const QString &module : std::as_const(m_modules)) {
            ReadoutClient::instance()->requestHistory(module, true);
        }
    }
}

QVariantMap ReadoutSource::shownData(const QString &module) const
{
    const ReadoutClient *client = ReadoutClient::instance();
    QVariantMap current = client->data(module);
    if (!m_detail || client->dataTier(module) == ReadoutClient::Tier::Detail) {
        return current;
    }
    // Until the first detail Update arrives, the last detail payload fills in what the panel
    // tier lacks (frequencies, processes, per-core values); fresh panel values win.
    QVariantMap shown = client->detailData(module);
    if (shown.isEmpty()) {
        return current;
    }
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        shown.insert(it.key(), it.value());
    }
    return shown;
}

void ReadoutSource::refreshData()
{
    bool changed = false;
    for (const QString &module : std::as_const(m_modules)) {
        const QVariantMap shown = shownData(module);
        if (!shown.isEmpty() && m_data.value(module).toMap() != shown) {
            m_data.insert(module, shown);
            changed = true;
        }
    }
    if (changed) {
        Q_EMIT dataChanged();
    }
}

void ReadoutSource::updateDetailReady()
{
    // Another widget may already hold these modules at the detail tier, so this can be true
    // right away; otherwise it turns true with the first detail Update.
    const ReadoutClient *client = ReadoutClient::instance();
    const bool ready = m_detail && std::all_of(m_modules.cbegin(), m_modules.cend(), [client](const QString &module) {
        return !ReadoutClient::isDaemonModule(module) || client->dataTier(module) == ReadoutClient::Tier::Detail;
    });
    if (ready != m_detailReady) {
        m_detailReady = ready;
        Q_EMIT detailReadyChanged();
    }
}

void ReadoutSource::setSensors(const QStringList &sensors)
{
    if (m_sensors == sensors) {
        return;
    }
    m_sensors = sensors;
    Q_EMIT sensorsChanged();
    publish();
}

void ReadoutSource::setIntervalMilliseconds(int interval)
{
    if (m_interval == interval) {
        return;
    }
    m_interval = interval;
    Q_EMIT intervalMillisecondsChanged();
    publish();
}

void ReadoutSource::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged();
    publish();
}

void ReadoutSource::setCores(bool cores)
{
    if (m_cores == cores) {
        return;
    }
    m_cores = cores;
    Q_EMIT coresChanged();
    publish();
}

QVariantMap ReadoutSource::info() const
{
    return ReadoutClient::instance()->info();
}

ReadoutSource::Status ReadoutSource::status() const
{
    return static_cast<Status>(ReadoutClient::instance()->status());
}

QString ReadoutSource::errorText() const
{
    return ReadoutClient::instance()->errorText();
}

QVariantList ReadoutSource::history(const QString &module, const QString &series) const
{
    return ReadoutClient::instance()->history(module, series);
}
