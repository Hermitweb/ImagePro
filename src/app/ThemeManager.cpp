#include "ThemeManager.h"
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTimer>

namespace yingtu {

namespace {
constexpr int kSystemThemePollIntervalMs = 1000;
// 与语言设置（ImageProApp 中的 "language" 键）保持同样的扁平键风格。
constexpr const char* kThemeSettingKey = "theme";
} // namespace

class ThemeManager::SystemThemeWatcher : public QObject
{
public:
    explicit SystemThemeWatcher(ThemeManager* manager) : QObject(manager), m_manager(manager)
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

    void stop() { m_timer->stop(); }

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

ThemeManager::ThemeManager(QObject* parent) : QObject(parent)
{
    // 读取持久化的主题选择。此前完全没写盘，用户选的亮色/暗色每次重启都会丢
    // （语言和窗口几何都持久化了，唯独主题没有）。
    const int stored =
        QSettings()
            .value(QString::fromLatin1(kThemeSettingKey), static_cast<int>(Theme::System))
            .toInt();
    switch (stored) {
    case static_cast<int>(Theme::Light):
    case static_cast<int>(Theme::Dark):
    case static_cast<int>(Theme::System):
        m_theme = static_cast<Theme>(stored);
        break;
    default:
        m_theme = Theme::System; // 配置被改坏时回退到默认值
        break;
    }
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
    QSettings().setValue(QString::fromLatin1(kThemeSettingKey), static_cast<int>(m_theme));
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
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
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
