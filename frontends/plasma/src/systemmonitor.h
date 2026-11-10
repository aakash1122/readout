// QML singleton "SystemMonitor": opens the desktop's full system monitor app from the popup header.
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

class SystemMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // False when no known system monitor is installed; the header then hides its button.
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit SystemMonitor(QObject *parent = nullptr);

    bool available() const { return !m_program.isEmpty(); }
    Q_INVOKABLE void open() const;

private:
    QString m_program;
};
