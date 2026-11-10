#include "readoutclient.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QTimer>

#include <KLocalizedString>

#include <algorithm>

namespace
{
const QString serviceName = QStringLiteral(READOUT_BUS_NAME);
const QString objectPath = QStringLiteral(READOUT_OBJECT_PATH);
const QString interfaceName = QStringLiteral(READOUT_BUS_NAME);

constexpr int retryDelayMilliseconds = 2000;

// Converts QtDBus containers (QDBusArgument, QDBusVariant) into plain QVariant maps and lists for QML.
QVariant toPlain(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return toPlain(value.value<QDBusVariant>().variant());
    }
    if (value.userType() != qMetaTypeId<QDBusArgument>()) {
        return value;
    }
    const QDBusArgument argument = value.value<QDBusArgument>();
    switch (argument.currentType()) {
    case QDBusArgument::MapType: {
        QVariantMap map;
        argument.beginMap();
        while (!argument.atEnd()) {
            // Values may be variants (a{sv}) or containers (a{sa{sv}}, a{sa(td)}): decode generically.
            argument.beginMapEntry();
            const QString key = argument.asVariant().toString();
            map.insert(key, toPlain(argument.asVariant()));
            argument.endMapEntry();
        }
        argument.endMap();
        return map;
    }
    case QDBusArgument::ArrayType: {
        QVariantList list;
        argument.beginArray();
        while (!argument.atEnd()) {
            list.append(toPlain(argument.asVariant()));
        }
        argument.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList list;
        argument.beginStructure();
        while (!argument.atEnd()) {
            list.append(toPlain(argument.asVariant()));
        }
        argument.endStructure();
        return list;
    }
    case QDBusArgument::BasicType:
    case QDBusArgument::VariantType:
        return toPlain(argument.asVariant());
    default:
        return {};
    }
}

ReadoutClient::Tier maxTier(ReadoutClient::Tier left, ReadoutClient::Tier right)
{
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

QString tierText(ReadoutClient::Tier tier)
{
    switch (tier) {
    case ReadoutClient::Tier::Panel:
        return QStringLiteral("panel");
    case ReadoutClient::Tier::Detail:
        return QStringLiteral("detail");
    case ReadoutClient::Tier::None:
        break;
    }
    return QStringLiteral("none");
}

const QStringList daemonModules = {
    QStringLiteral("cpu"),
    QStringLiteral("memory"),
    QStringLiteral("network"),
    QStringLiteral("disk"),
    QStringLiteral("gpu"),
    QStringLiteral("sensors"),
    QStringLiteral("battery"),
    QStringLiteral("bluetooth"),
};
} // namespace

ReadoutClient *ReadoutClient::instance()
{
    // One client per process, shared by all applet instances and QML engines.
    static ReadoutClient *client = new ReadoutClient();
    return client;
}

ReadoutClient::ReadoutClient(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<QMap<QString, QString>>();

    m_subscribeTimer = new QTimer(this);
    m_subscribeTimer->setSingleShot(true);
    m_subscribeTimer->setInterval(0);
    connect(m_subscribeTimer, &QTimer::timeout, this, &ReadoutClient::sendSubscribe);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    m_retryTimer->setInterval(retryDelayMilliseconds);
    connect(m_retryTimer, &QTimer::timeout, this, &ReadoutClient::connectToService);

    QDBusConnection bus = QDBusConnection::sessionBus();
    m_watcher = new QDBusServiceWatcher(serviceName, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &newOwner) {
        if (newOwner.isEmpty()) {
            // The daemon went away (crash, upgrade, or idle exit while we had no consumers).
            m_info.clear();
            if (!m_consumers.isEmpty()) {
                setStatus(Status::Connecting);
                m_retryTimer->start();
            }
        } else if (!m_consumers.isEmpty()) {
            connectToService();
        }
    });
    bus.connect(serviceName, objectPath, interfaceName, QStringLiteral("Update"), this, SLOT(handleUpdate(QDBusMessage)));
}

void ReadoutClient::setStatus(Status status, const QString &errorText)
{
    if (m_status == status && m_errorText == errorText) {
        return;
    }
    m_status = status;
    m_errorText = errorText;
    Q_EMIT statusChanged();
}

void ReadoutClient::connectToService()
{
    if (m_infoPending) {
        return;
    }
    m_infoPending = true;
    // GetInfo auto-starts the daemon through D-Bus activation.
    const QDBusMessage call = QDBusMessage::createMethodCall(serviceName, objectPath, interfaceName, QStringLiteral("GetInfo"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        m_infoPending = false;
        const QDBusPendingReply<QVariantMap> reply = *finished;
        if (reply.isError()) {
            const QString name = reply.error().name();
            if (name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")) {
                setStatus(Status::NotInstalled, i18n("The readoutd service is not installed."));
            } else {
                setStatus(Status::Error, reply.error().message());
                m_retryTimer->start();
            }
            return;
        }
        QVariantMap info;
        const QVariantMap raw = reply.value();
        for (auto it = raw.cbegin(); it != raw.cend(); ++it) {
            info.insert(it.key(), toPlain(it.value()));
        }
        m_info = info;
        m_connected = true;
        Q_EMIT infoChanged();
        setStatus(Status::Ready);
        m_historyRequested.clear();
        sendSubscribe();
    });
}

int ReadoutClient::addConsumer()
{
    const int id = m_nextConsumerId++;
    m_consumers.insert(id, Consumer{});
    if (m_consumers.size() == 1 && m_info.isEmpty()) {
        connectToService();
    }
    return id;
}

void ReadoutClient::removeConsumer(int id)
{
    if (m_consumers.remove(id) > 0) {
        scheduleSubscribe();
    }
}

void ReadoutClient::setConsumer(int id, const Consumer &consumer)
{
    auto it = m_consumers.find(id);
    if (it == m_consumers.end()) {
        return;
    }
    if (it->modules == consumer.modules && it->sensors == consumer.sensors && it->intervalMilliseconds == consumer.intervalMilliseconds
        && it->cores == consumer.cores) {
        return;
    }
    *it = consumer;
    scheduleSubscribe();
}

void ReadoutClient::scheduleSubscribe()
{
    // Coalesces changes from many instances (startup, popup open) into one call.
    m_subscribeTimer->start();
}

void ReadoutClient::sendSubscribe()
{
    if (m_status != Status::Ready) {
        if (!m_consumers.isEmpty() && m_status != Status::NotInstalled) {
            connectToService();
        }
        return;
    }
    QHash<QString, Tier> merged;
    QStringList sensors;
    int interval = 0;
    bool cores = false;
    for (const Consumer &consumer : std::as_const(m_consumers)) {
        cores = cores || consumer.cores;
        for (auto it = consumer.modules.cbegin(); it != consumer.modules.cend(); ++it) {
            if (daemonModules.contains(it.key())) {
                merged[it.key()] = maxTier(merged.value(it.key(), Tier::None), it.value());
            }
        }
        for (const QString &sensor : consumer.sensors) {
            if (!sensors.contains(sensor)) {
                sensors.append(sensor);
            }
        }
        if (!consumer.modules.isEmpty()) {
            interval = interval == 0 ? consumer.intervalMilliseconds : std::min(interval, consumer.intervalMilliseconds);
        }
    }
    QMap<QString, QString> modules;
    for (const QString &module : daemonModules) {
        modules.insert(module, tierText(merged.value(module, Tier::None)));
    }
    QVariantMap options;
    options.insert(QStringLiteral("sensors"), sensors);
    options.insert(QStringLiteral("cores"), cores);

    QDBusMessage call = QDBusMessage::createMethodCall(serviceName, objectPath, interfaceName, QStringLiteral("Subscribe"));
    call << QVariant::fromValue(modules) << static_cast<uint>(interval > 0 ? interval : 2000) << options;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, merged](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<> reply = *finished;
        if (reply.isError()) {
            setStatus(Status::Error, reply.error().message());
            m_retryTimer->start();
            return;
        }
        // The daemon replies after applying the subscription, and the bus keeps its messages in
        // order, so every Update received from now on was sampled at these tiers.
        m_confirmedTiers = merged;
    });
}

bool ReadoutClient::isDaemonModule(const QString &module)
{
    return daemonModules.contains(module);
}

void ReadoutClient::handleUpdate(const QDBusMessage &message)
{
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() != 2) {
        return;
    }
    const QVariant modules = toPlain(arguments.at(1));
    const QVariantMap map = modules.toMap();
    const qint64 timestamp = arguments.at(0).toLongLong(); // CLOCK_MONOTONIC, microseconds
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const QVariantMap payload = it.value().toMap();
        m_data.insert(it.key(), payload);
        m_lastUpdate.insert(it.key(), timestamp);
        const Tier tier = m_confirmedTiers.value(it.key(), Tier::Panel);
        m_dataTiers.insert(it.key(), tier);
        if (tier == Tier::Detail) {
            m_detailData.insert(it.key(), payload);
        }
        appendHistory(it.key(), payload);
        Q_EMIT moduleUpdated(it.key());
    }
}

void ReadoutClient::pushHistoryValue(const QString &key, double value)
{
    QVariantList &values = m_history[key];
    values.append(value);
    if (values.size() > historyLength) {
        values.removeFirst();
    }
}

void ReadoutClient::appendHistory(const QString &module, const QVariantMap &payload)
{
    const auto push = [&](const char *series, const char *field) {
        const auto value = payload.constFind(QLatin1String(field));
        if (value != payload.cend()) {
            pushHistoryValue(module + QLatin1Char('/') + QLatin1String(series), value->toDouble());
        }
    };
    if (module == QLatin1String("cpu") || module == QLatin1String("memory")) {
        push("usage", "usage");
    } else if (module == QLatin1String("network")) {
        push("download", "download");
        push("upload", "upload");
    } else if (module == QLatin1String("disk")) {
        push("read", "read");
        push("write", "write");
        // Per-drive values arrive only in the detail tier; GetHistory fills the gaps when a popup opens.
        const QVariantList disks = payload.value(QStringLiteral("disks")).toList();
        for (const QVariant &entry : disks) {
            const QVariantMap disk = entry.toMap();
            const QString prefix = module + QLatin1Char('/') + disk.value(QStringLiteral("name")).toString();
            pushHistoryValue(prefix + QStringLiteral("/read"), disk.value(QStringLiteral("read")).toDouble());
            pushHistoryValue(prefix + QStringLiteral("/write"), disk.value(QStringLiteral("write")).toDouble());
        }
    } else if (module == QLatin1String("battery")) {
        push("level", "percentage");
    } else if (module == QLatin1String("gpu")) {
        const QVariantList gpus = payload.value(QStringLiteral("gpus")).toList();
        for (const QVariant &entry : gpus) {
            const QVariantMap gpu = entry.toMap();
            const auto usage = gpu.constFind(QStringLiteral("usage"));
            if (usage != gpu.cend()) {
                pushHistoryValue(module + QLatin1Char('/') + gpu.value(QStringLiteral("id")).toString() + QStringLiteral("/usage"), usage->toDouble());
            }
        }
    } else if (module == QLatin1String("sensors")) {
        const QVariantList sensors = payload.value(QStringLiteral("sensors")).toList();
        for (const QVariant &entry : sensors) {
            const QVariantMap sensor = entry.toMap();
            if (sensor.value(QStringLiteral("valid")).toBool()) {
                pushHistoryValue(module + QLatin1Char('/') + sensor.value(QStringLiteral("id")).toString(), sensor.value(QStringLiteral("value")).toDouble());
            }
        }
    }
    Q_EMIT historyUpdated(module);
}

QVariantList ReadoutClient::history(const QString &module, const QString &series) const
{
    return m_history.value(module + QLatin1Char('/') + series);
}

void ReadoutClient::requestHistory(const QString &module, bool refresh)
{
    if (m_status != Status::Ready || (m_historyRequested.contains(module) && !refresh) || !daemonModules.contains(module)) {
        return;
    }
    m_historyRequested.insert(module);
    QDBusMessage call = QDBusMessage::createMethodCall(serviceName, objectPath, interfaceName, QStringLiteral("GetHistory"));
    call << module;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, module](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<> reply = *finished;
        if (reply.isError() || reply.reply().arguments().isEmpty()) {
            return;
        }
        const QVariantMap series = toPlain(reply.reply().arguments().constFirst()).toMap();
        for (auto it = series.cbegin(); it != series.cend(); ++it) {
            QVariantList values;
            const QVariantList points = it.value().toList();
            values.reserve(points.size());
            for (const QVariant &point : points) {
                const QVariantList pair = point.toList();
                if (pair.size() == 2) {
                    values.append(pair.at(1).toDouble());
                }
            }
            const QString key = module + QLatin1Char('/') + it.key();
            // The daemon's history is complete, so it wins unless ours is longer (daemon restarted).
            if (values.size() >= m_history.value(key).size()) {
                while (values.size() > historyLength) {
                    values.removeFirst();
                }
                m_history.insert(key, values);
            }
        }
        Q_EMIT historyUpdated(module);
    });
}
