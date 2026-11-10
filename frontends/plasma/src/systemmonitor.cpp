#include "systemmonitor.h"

#include <QProcess>
#include <QStandardPaths>

SystemMonitor::SystemMonitor(QObject *parent)
    : QObject(parent)
{
    // Plasma's own monitor first, then older or other desktops' monitors.
    for (const char *program : {"plasma-systemmonitor", "ksysguard", "gnome-system-monitor"}) {
        m_program = QStandardPaths::findExecutable(QString::fromLatin1(program));
        if (!m_program.isEmpty()) {
            break;
        }
    }
}

void SystemMonitor::open() const
{
    if (!m_program.isEmpty()) {
        QProcess::startDetached(m_program, {});
    }
}
