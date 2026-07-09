#include "ThemeManager.h"
#include <QApplication>
#include <QFile>
#include <QPalette>

namespace yingtu {

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    m_theme = Theme::Light;
}

Theme ThemeManager::currentTheme() const
{
    return m_theme;
}

void ThemeManager::setTheme(Theme theme)
{
    if (m_theme == theme)
        return;
    m_theme = theme;
    applyCurrentTheme();
    emit themeChanged(theme);
}

void ThemeManager::toggleTheme()
{
    if (m_theme == Theme::Dark)
        setTheme(Theme::Light);
    else
        setTheme(Theme::Dark);
}

void ThemeManager::applyCurrentTheme()
{
    QString qssPath = themeFilePath(m_theme);
    if (qssPath.isEmpty()) {
        qApp->setStyleSheet(QString());
        return;
    }

    QFile file(qssPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        qApp->setStyleSheet(QString::fromUtf8(data));
    } else {
        qApp->setStyleSheet(QString());
    }
}

bool ThemeManager::isDark() const
{
    return m_theme == Theme::Dark;
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

} // namespace yingtu
