#include "ThemeManager.h"
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTimer>

namespace yingtu {

namespace {
    constexpr int kSystemThemePollIntervalMs = 1000;
}

class ThemeManager::SystemThemeWatcher : public QObject
{
public:
    explicit SystemThemeWatcher(ThemeManager* manager)
        : QObject(manager)
        , m_manager(manager)
    {
        m_timer = new QTimer(this);
        m_timer->setInterval(kSystemThemePollIntervalMs);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            if (!m_manager)
                return;
            const bool light = m_manager->systemUsesLightTheme();
            if (light != m_lastLight) {
                m_lastLight = light;
                m_manager->onSystemThemeChanged();
            }
        });
    }

    void start()
    {
        if (!m_manager)
            return;
        m_lastLight = m_manager->systemUsesLightTheme();
        m_timer->start();
    }

    void stop()
    {
        m_timer->stop();
    }

private:
    ThemeManager* m_manager = nullptr;
    QTimer* m_timer = nullptr;
    bool m_lastLight = true;
};

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    m_theme = Theme::System;
    updateSystemWatcher();
}

ThemeManager::~ThemeManager() = default;

Theme ThemeManager::currentTheme() const
{
    return m_theme;
}

void ThemeManager::setTheme(Theme theme)
{
    if (m_theme == theme)
        return;

    m_theme = theme;
    updateSystemWatcher();
    applyCurrentTheme();
    emit themeChanged(m_theme);
}

void ThemeManager::toggleTheme()
{
    switch (m_theme) {
    case Theme::Light:
        setTheme(Theme::Dark);
        break;
    case Theme::Dark:
        setTheme(Theme::System);
        break;
    case Theme::System:
    default:
        setTheme(Theme::Light);
        break;
    }
}

void ThemeManager::applyCurrentTheme()
{
    const Theme theme = effectiveTheme();
    const QString qssPath = themeFilePath(theme);

    QFile file(qssPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        qApp->setStyleSheet(QString::fromUtf8(data));
    } else {
        qApp->setStyleSheet(QString());
    }
}

bool ThemeManager::isDark() const
{
    return effectiveTheme() == Theme::Dark;
}

Theme ThemeManager::effectiveTheme() const
{
    switch (m_theme) {
    case Theme::Light:
        return Theme::Light;
    case Theme::Dark:
        return Theme::Dark;
    case Theme::System:
    default:
        return systemUsesLightTheme() ? Theme::Light : Theme::Dark;
    }
}

QString ThemeManager::themeFilePath(Theme theme) const
{
    switch (theme) {
    case Theme::Light:
        return QStringLiteral(":/themes/light.qss");
    case Theme::Dark:
        return QStringLiteral(":/themes/dark.qss");
    default:
        return QStringLiteral(":/themes/light.qss");
    }
}

bool ThemeManager::systemUsesLightTheme() const
{
#ifdef Q_OS_WIN
    const QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return settings.value(QStringLiteral("AppsUseLightTheme"), 1).toBool();
#else
    return true;
#endif
}

void ThemeManager::updateSystemWatcher()
{
    if (m_theme == Theme::System) {
        if (!m_systemWatcher)
            m_systemWatcher = new SystemThemeWatcher(this);
        m_systemWatcher->start();
    } else if (m_systemWatcher) {
        m_systemWatcher->stop();
    }
}

void ThemeManager::onSystemThemeChanged()
{
    if (m_theme != Theme::System)
        return;

    applyCurrentTheme();
    emit themeChanged(m_theme);
}

} // namespace yingtu
