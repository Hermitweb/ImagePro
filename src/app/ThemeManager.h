#pragma once

#include <QObject>
#include <QString>

namespace yingtu {

enum class Theme {
    Light,
    Dark,
    System
};

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager& instance();

    Theme currentTheme() const;
    void setTheme(Theme theme);
    void toggleTheme();
    void applyCurrentTheme();

    bool isDark() const;

signals:
    void themeChanged(Theme theme);

private:
    ThemeManager(QObject* parent = nullptr);
    QString themeFilePath(Theme theme) const;
    Theme m_theme = Theme::Light;
};

} // namespace yingtu
