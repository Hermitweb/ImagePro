#include "ImageProApp.h"
#include "ThemeManager.h"
#include <QDir>
#include <QFile>
#include <QFont>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

namespace yingtu {

ImageProApp::ImageProApp(int& argc, char** argv)
    : QApplication(argc, argv)
{
    setOrganizationName(QStringLiteral("yingtu"));
    setApplicationName(QStringLiteral("影图 ImagePro"));
    setApplicationDisplayName(QStringLiteral("影图 ImagePro"));
}

static void logLanguageStep(const QString& step)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QFile log(QDir(dir).absoluteFilePath(QStringLiteral("app_debug.log")));
    if (log.open(QIODevice::WriteOnly | QIODevice::Append)) {
        log.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        log.write(" ");
        log.write(step.toUtf8());
        log.write("\n");
    }
}

void ImageProApp::initialize()
{
    logLanguageStep(QStringLiteral("initialize start"));
    QFont font(QStringLiteral("Microsoft YaHei"), 9);
    setFont(font);

    QSettings settings;
    QString lang = settings.value(QStringLiteral("language"), QStringLiteral("zh_CN")).toString();
    if (!supportedLanguages().contains(lang))
        lang = QStringLiteral("zh_CN");
    setLanguage(lang);

    ThemeManager::instance().applyCurrentTheme();
}

QStringList ImageProApp::supportedLanguages()
{
    return QStringList() << QStringLiteral("zh_CN") << QStringLiteral("en");
}

QString ImageProApp::languageName(const QString& code)
{
    if (code == QStringLiteral("zh_CN"))
        return QStringLiteral("中文");
    if (code == QStringLiteral("en"))
        return QStringLiteral("English");
    return code;
}

void ImageProApp::setLanguage(const QString& code)
{
    logLanguageStep(QStringLiteral("setLanguage start: ") + code);
    if (m_currentLanguage == code) {
        logLanguageStep(QStringLiteral("same language, return"));
        return;
    }

    // 卸载旧翻译
    if (m_appTranslator) {
        logLanguageStep(QStringLiteral("remove app translator"));
        removeTranslator(m_appTranslator);
        logLanguageStep(QStringLiteral("delete app translator"));
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        logLanguageStep(QStringLiteral("remove qt translator"));
        removeTranslator(m_qtTranslator);
        logLanguageStep(QStringLiteral("delete qt translator"));
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    m_currentLanguage = code;
    logLanguageStep(QStringLiteral("set current language"));

    // 安装应用翻译
    logLanguageStep(QStringLiteral("create app translator"));
    m_appTranslator = new QTranslator(this);
    const QString appQmPath = QStringLiteral(":/i18n/ImagePro_") + code;
    logLanguageStep(QStringLiteral("load app translator: ") + appQmPath);
    if (m_appTranslator->load(appQmPath)) {
        logLanguageStep(QStringLiteral("install app translator"));
        installTranslator(m_appTranslator);
    } else {
        logLanguageStep(QStringLiteral("app translator load failed"));
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }

    // 安装 Qt 标准控件翻译
    logLanguageStep(QStringLiteral("create qt translator"));
    m_qtTranslator = new QTranslator(this);
    QString qtQm = QStringLiteral("qt_") + code;
    logLanguageStep(QStringLiteral("load qt translator path1"));
    bool qtLoaded = m_qtTranslator->load(qtQm, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    logLanguageStep(QStringLiteral("qt path1 result: ") + QString::number(qtLoaded));
    if (!qtLoaded) {
        logLanguageStep(QStringLiteral("load qt translator path2"));
        qtLoaded = m_qtTranslator->load(qtQm, applicationDirPath() + QStringLiteral("/translations"));
        logLanguageStep(QStringLiteral("qt path2 result: ") + QString::number(qtLoaded));
    }
    if (!qtLoaded) {
        logLanguageStep(QStringLiteral("load qt translator path3"));
        qtLoaded = m_qtTranslator->load(qtQm, applicationDirPath());
        logLanguageStep(QStringLiteral("qt path3 result: ") + QString::number(qtLoaded));
    }
    if (qtLoaded) {
        logLanguageStep(QStringLiteral("install qt translator"));
        installTranslator(m_qtTranslator);
    } else {
        logLanguageStep(QStringLiteral("delete qt translator"));
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    logLanguageStep(QStringLiteral("write settings"));
    QSettings settings;
    settings.setValue(QStringLiteral("language"), code);
    logLanguageStep(QStringLiteral("setLanguage end"));
}

QString ImageProApp::currentLanguage() const
{
    return m_currentLanguage;
}

} // namespace yingtu
