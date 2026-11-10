// One D-Bus client for every Readout widget in this plasmashell.
//
// Widget instances register what they need (module tiers, sensors, interval) as "consumers".
// The client merges all consumers into a single Subscribe() call, so ten widgets cost the
// daemon exactly as much as one, and only one Update signal arrives per interval.
#pragma once

#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QDBusServiceWatcher;
class QTimer;

class ReadoutClient : public QObject
{
    Q_OBJECT
public:
    enum class Status {
        Connecting,
        Ready,
        NotInstalled,
        Error,
    };
    Q_ENUM(Status)

    enum class Tier {
        None = 0,
        Panel = 1,
        Detail = 2,
    };

    struct Consumer {
        QHash<QString, Tier> modules;
        QStringList sensors;
        int intervalMilliseconds = 2000;
        bool cores = false; // per-core cpu usage in the panel tier
    };

    static ReadoutClient *instance();

    Status status() const { return m_status; }
    QString errorText() const { return m_errorText; }
    QVariantMap info() const { return m_info; }
    QVariantMap data(const QString &module) const { return m_data.value(module); }
    QVariantList history(const QString &module, const QString &series) const;
    qint64 lastUpdate(const QString &module) const { return m_lastUpdate.value(module); }
    // The tier of the module's latest Update: the tier confirmed by the last Subscribe reply that
    // arrived before it. A popup uses it to tell panel data from the detail data it waits for.
    Tier dataTier(const QString &module) const { return m_dataTiers.value(module, Tier::None); }
    static bool isDaemonModule(const QString &module);
    // The module's latest detail-tier payload, kept after the popup closes. A popup that opens
    // shows it (under fresh panel-tier values) until its own first detail Update arrives.
    QVariantMap detailData(const QString &module) const { return m_detailData.value(module); }

    int addConsumer();
    void removeConsumer(int id);
    void setConsumer(int id, const Consumer &consumer);
    // Fetches the daemon's history for `module`, so charts start full. Only the first call
    // fetches unless `refresh` is set (a popup opening, to fill gaps left while in the panel tier).
    void requestHistory(const QString &module, bool refresh = false);

    static constexpr int historyLength = 180;

Q_SIGNALS:
    void statusChanged();
    void infoChanged();
    void moduleUpdated(const QString &module);
    void historyUpdated(const QString &module);

private Q_SLOTS:
    void handleUpdate(const QDBusMessage &message);

private:
    explicit ReadoutClient(QObject *parent = nullptr);
    void setStatus(Status status, const QString &errorText = {});
    void connectToService();
    void scheduleSubscribe();
    void sendSubscribe();
    void appendHistory(const QString &module, const QVariantMap &payload);
    void pushHistoryValue(const QString &key, double value);

    QDBusServiceWatcher *m_watcher = nullptr;
    QTimer *m_subscribeTimer = nullptr;
    QTimer *m_retryTimer = nullptr;
    Status m_status = Status::Connecting;
    QString m_errorText;
    QVariantMap m_info;
    QHash<QString, QVariantMap> m_data;
    QHash<QString, qint64> m_lastUpdate;
    QHash<QString, Tier> m_confirmedTiers;
    QHash<QString, Tier> m_dataTiers;
    QHash<QString, QVariantMap> m_detailData;
    QHash<QString, QVariantList> m_history; // "module/series" -> values, oldest first
    QSet<QString> m_historyRequested;
    QHash<int, Consumer> m_consumers;
    int m_nextConsumerId = 1;
    bool m_connected = false;
    bool m_infoPending = false;
};
