import QtQuick

import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18nc("@title:group config page", "General")
        icon: "preferences-system"
        source: "ConfigGeneral.qml"
    }
    ConfigCategory {
        name: i18nc("@title:group config page", "Alerts")
        icon: "preferences-desktop-notification"
        source: "ConfigAlerts.qml"
    }
}
