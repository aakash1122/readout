#include "settings.h"

#include <KConfigGroup>

#include <algorithm>

namespace
{
constexpr auto groupName = "General";
constexpr auto intervalKey = "UpdateInterval";
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("readoutrc"), KConfig::SimpleConfig))
    , m_watcher(KConfigWatcher::create(m_config))
{
    load();
    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == QLatin1String(groupName)) {
            load();
        }
    });
}

int Settings::read(const Range &range) const
{
    return std::clamp(m_config->group(QLatin1String(groupName)).readEntry(range.key, range.fallback), range.minimum, range.maximum);
}

void Settings::load()
{
    const int interval = std::clamp(m_config->group(QLatin1String(groupName)).readEntry(intervalKey, defaultInterval), minimumInterval, maximumInterval);
    if (interval != m_updateInterval) {
        m_updateInterval = interval;
        Q_EMIT updateIntervalChanged();
    }

    bool popupChangedNow = false;
    for (auto [range, member] : {std::pair{&textScaleRange, &m_popupTextScale}, std::pair{&spacingRange, &m_popupSpacing},
                                 std::pair{&marginRange, &m_popupMargin}, std::pair{&widthRange, &m_popupWidth}}) {
        const int value = read(*range);
        if (value != *member) {
            *member = value;
            popupChangedNow = true;
        }
    }
    if (popupChangedNow) {
        Q_EMIT popupChanged();
    }
}

void Settings::setUpdateInterval(int seconds)
{
    seconds = std::clamp(seconds, minimumInterval, maximumInterval);
    if (seconds == m_updateInterval) {
        return;
    }
    m_updateInterval = seconds;
    KConfigGroup group = m_config->group(QLatin1String(groupName));
    // Notify so other processes (another plasmashell, plasmoidviewer) pick it up too.
    group.writeEntry(intervalKey, seconds, KConfig::Notify);
    m_config->sync();
    Q_EMIT updateIntervalChanged();
}

bool Settings::write(const Range &range, int value, int &member)
{
    value = std::clamp(value, range.minimum, range.maximum);
    if (value == member) {
        return false;
    }
    member = value;
    m_config->group(QLatin1String(groupName)).writeEntry(range.key, value, KConfig::Notify);
    m_config->sync();
    return true;
}

void Settings::setPopupTextScale(int percent)
{
    if (write(textScaleRange, percent, m_popupTextScale)) {
        Q_EMIT popupChanged();
    }
}

void Settings::setPopupSpacing(int spacing)
{
    if (write(spacingRange, spacing, m_popupSpacing)) {
        Q_EMIT popupChanged();
    }
}

void Settings::setPopupMargin(int points)
{
    if (write(marginRange, points, m_popupMargin)) {
        Q_EMIT popupChanged();
    }
}

void Settings::setPopupWidth(int width)
{
    if (write(widthRange, width, m_popupWidth)) {
        Q_EMIT popupChanged();
    }
}

QVariantMap Settings::popupDefaults() const
{
    return {
        {QStringLiteral("textScale"), textScaleRange.fallback},
        {QStringLiteral("spacing"), spacingRange.fallback},
        {QStringLiteral("margin"), marginRange.fallback},
        {QStringLiteral("width"), widthRange.fallback},
    };
}
