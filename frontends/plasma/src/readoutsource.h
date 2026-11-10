// QML element: the data of one or more modules for one widget instance.
//
//   ReadoutSource {
//       modules: ["cpu"]
//       detail: root.expanded      // popup open -> detail tier
//       intervalMilliseconds: 2000
//   }
//   ... source.data.cpu.usage
//   HistorySeries { source: source; module: "cpu"; series: "usage" } for charts
#pragma once

#include "readoutclient.h"

#include <QObject>
#include <QQmlEngine>
#include <QQmlParserStatus>

class ReadoutSource : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)

    Q_PROPERTY(QStringList modules READ modules WRITE setModules NOTIFY modulesChanged)
    Q_PROPERTY(bool detail READ detail WRITE setDetail NOTIFY detailChanged)
    // True once every daemon module of this source has delivered detail-tier data since
    // `detail` was set; false while the popup still shows panel-tier data.
    Q_PROPERTY(bool detailReady READ detailReady NOTIFY detailReadyChanged)
    Q_PROPERTY(QStringList sensors READ sensors WRITE setSensors NOTIFY sensorsChanged)
    Q_PROPERTY(int intervalMilliseconds READ intervalMilliseconds WRITE setIntervalMilliseconds NOTIFY intervalMillisecondsChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // Ask for per-core cpu usage in the panel tier (cpu "bars" style).
    Q_PROPERTY(bool cores READ cores WRITE setCores NOTIFY coresChanged)

    // module name -> latest payload (see docs/dbus-api.md)
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    // module name -> static info from GetInfo()
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)

public:
    enum Status {
        Connecting,
        Ready,
        NotInstalled,
        Error,
    };
    Q_ENUM(Status)

    explicit ReadoutSource(QObject *parent = nullptr);
    ~ReadoutSource() override;

    QStringList modules() const { return m_modules; }
    void setModules(const QStringList &modules);
    bool detail() const { return m_detail; }
    bool detailReady() const { return m_detailReady; }
    void setDetail(bool detail);
    QStringList sensors() const { return m_sensors; }
    void setSensors(const QStringList &sensors);
    int intervalMilliseconds() const { return m_interval; }
    void setIntervalMilliseconds(int interval);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool cores() const { return m_cores; }
    void setCores(bool cores);

    QVariantMap data() const { return m_data; }
    QVariantMap info() const;
    Status status() const;
    QString errorText() const;

    // Values of one series, oldest first. Series: cpu/memory "usage", network "download"/"upload",
    // disk "read"/"write", battery "level", gpu "<id>/usage", sensors "<sensor id>".
    Q_INVOKABLE QVariantList history(const QString &module, const QString &series) const;

    void classBegin() override;
    void componentComplete() override;

Q_SIGNALS:
    void modulesChanged();
    void detailChanged();
    void detailReadyChanged();
    void sensorsChanged();
    void intervalMillisecondsChanged();
    void activeChanged();
    void coresChanged();
    void dataChanged();
    void infoChanged();
    void statusChanged();
    // The history of `module` changed (HistorySeries listens to this).
    void historyChanged(const QString &module);
    // Emitted once per daemon update of `module` (also when the value did not change).
    void updated(const QString &module);

private:
    void publish();
    void updateDetailReady();
    QVariantMap shownData(const QString &module) const;
    void refreshData();

    QStringList m_modules;
    QStringList m_sensors;
    bool m_detail = false;
    bool m_detailReady = false;
    bool m_active = true;
    bool m_cores = false;
    int m_interval = 2000;
    bool m_complete = false;
    int m_consumerId = 0;
    QVariantMap m_data;
};
