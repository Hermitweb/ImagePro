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
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override;

    Theme effectiveTheme() const;
    QString themeFilePath(Theme theme) const;
    bool systemUsesLightTheme() const;
    void updateSystemWatcher();
    void onSystemThemeChanged();

    Theme m_theme = Theme::System;
    class SystemThemeWatcher;
    SystemThemeWatcher* m_systemWatcher = nullptr;
};

} // namespace yingtu
