// QML singleton "Settings": options shared by every Readout widget (spec section 3, "Global").
// Stored in ~/.config/readoutrc and watched, so a change applies to all widgets at once.
#pragma once

#include <KConfigWatcher>
#include <KSharedConfig>

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

class Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Update interval in seconds, 1 to 10.
    Q_PROPERTY(int updateInterval READ updateInterval WRITE setUpdateInterval NOTIFY updateIntervalChanged)
    Q_PROPERTY(int minimumInterval MEMBER minimumIntervalValue CONSTANT)
    Q_PROPERTY(int maximumInterval MEMBER maximumIntervalValue CONSTANT)

    // Popup appearance, shared by every popup (docs/frontend-spec.md, "Popup").
    // Text size in percent of the default, 80 to 130.
    Q_PROPERTY(int popupTextScale READ popupTextScale WRITE setPopupTextScale NOTIFY popupChanged)
    // 0 compact, 1 normal (Stats), 2 comfortable.
    Q_PROPERTY(int popupSpacing READ popupSpacing WRITE setPopupSpacing NOTIFY popupChanged)
    // Gap between the popup edge and its content, in Stats points, 0 to 16.
    Q_PROPERTY(int popupMargin READ popupMargin WRITE setPopupMargin NOTIFY popupChanged)
    // 0 narrow, 1 normal (Stats), 2 wide.
    Q_PROPERTY(int popupWidth READ popupWidth WRITE setPopupWidth NOTIFY popupChanged)
    // Default of each popup setting: {textScale, spacing, margin, width}.
    Q_PROPERTY(QVariantMap popupDefaults READ popupDefaults CONSTANT)

public:
    explicit Settings(QObject *parent = nullptr);

    static constexpr int minimumInterval = 1;
    static constexpr int maximumInterval = 10;
    static constexpr int defaultInterval = 2;

    int updateInterval() const { return m_updateInterval; }
    void setUpdateInterval(int seconds);

    int popupTextScale() const { return m_popupTextScale; }
    void setPopupTextScale(int percent);
    int popupSpacing() const { return m_popupSpacing; }
    void setPopupSpacing(int spacing);
    int popupMargin() const { return m_popupMargin; }
    void setPopupMargin(int points);
    int popupWidth() const { return m_popupWidth; }
    void setPopupWidth(int width);

    QVariantMap popupDefaults() const;

Q_SIGNALS:
    void updateIntervalChanged();
    void popupChanged();

private:
    struct Range {
        const char *key;
        int minimum;
        int maximum;
        int fallback;
    };
    static constexpr Range textScaleRange{"PopupTextScale", 80, 130, 100};
    static constexpr Range spacingRange{"PopupSpacing", 0, 2, 1};
    static constexpr Range marginRange{"PopupMargin", 0, 16, 8};
    static constexpr Range widthRange{"PopupWidth", 0, 2, 1};

    void load();
    int read(const Range &range) const;
    // Clamps, stores and notifies other processes. Returns false when nothing changed.
    bool write(const Range &range, int value, int &member);

    KSharedConfig::Ptr m_config;
    KConfigWatcher::Ptr m_watcher;
    int m_updateInterval = defaultInterval;
    int m_popupTextScale = textScaleRange.fallback;
    int m_popupSpacing = spacingRange.fallback;
    int m_popupMargin = marginRange.fallback;
    int m_popupWidth = widthRange.fallback;
    const int minimumIntervalValue = minimumInterval;
    const int maximumIntervalValue = maximumInterval;
};
