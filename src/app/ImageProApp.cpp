#include "ImageProApp.h"
#include "ThemeManager.h"
#include "utils/ImageLoader.h"
#include <QFont>
#include <QLibraryInfo>
#include <QSettings>
#include <QStyleFactory>
#include <QTranslator>

namespace yingtu {

ImageProApp::ImageProApp(int& argc, char** argv)
    : QApplication(argc, argv)
{
    setOrganizationName(QStringLiteral("yingtu"));
    setApplicationName(QStringLiteral("影图"));
    setApplicationDisplayName(QStringLiteral("影图"));
}

void ImageProApp::initialize()
{
    setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont font(QStringLiteral("Microsoft YaHei"), 9);
    setFont(font);

    // libvips 必须在主线程完成首次初始化，避免在 QtConcurrent 工作线程中
    // 首次调用时触发 GLib/GObject 线程问题，导致拼接/尺寸预览闪退或挂起。
    ImageLoader::initialize();

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
    if (m_currentLanguage == code)
        return;

    // 卸载旧翻译
    if (m_appTranslator) {
        removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    m_currentLanguage = code;

    // 安装应用翻译
    m_appTranslator = new QTranslator(this);
    const QString appQmPath = QStringLiteral(":/i18n/ImagePro_") + code;
    if (m_appTranslator->load(appQmPath)) {
        installTranslator(m_appTranslator);
    } else {
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }

    // 安装 Qt 标准控件翻译
    m_qtTranslator = new QTranslator(this);
    QString qtQm = QStringLiteral("qt_") + code;
    bool qtLoaded = m_qtTranslator->load(qtQm, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    if (!qtLoaded)
        qtLoaded = m_qtTranslator->load(qtQm, applicationDirPath() + QStringLiteral("/translations"));
    if (!qtLoaded)
        qtLoaded = m_qtTranslator->load(qtQm, applicationDirPath());
    if (qtLoaded) {
        installTranslator(m_qtTranslator);
    } else {
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("language"), code);
}

QString ImageProApp::currentLanguage() const
{
    return m_currentLanguage;
}

} // namespace yingtu
