#include "ImageProApp.h"
#include "ThemeManager.h"
#include "utils/ErrorHandler.h"
#include "utils/ImageLoader.h"
#include "utils/ImageProError.h"
#include <QFont>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStyleFactory>
#include <QTranslator>

namespace yingtu {

namespace {
// 语言切换闸门。装/卸翻译器各触发一次 LanguageChange，一次切换会连发好几次；
// 期间控件据此跳过重译，等切换结束后由应用统一补发一次。
bool g_languageSwitching = false;
} // namespace

bool ImageProApp::isLanguageSwitching()
{
    return g_languageSwitching;
}

ImageProApp::ImageProApp(int& argc, char** argv) : QApplication(argc, argv)
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
    // 初始化失败时所有图像操作都会失败，必须明确告知用户——否则界面看起来
    // 一切正常，实际每次加载/转换/拼接都在静默失败。
    if (!ImageLoader::initialize()) {
        // 同时经 ErrorHandler 上报，使错误码（IMGPRO-4001）进入日志，
        // 而不是只弹一个对话框、事后无从排查。
        ErrorHandler::instance().report(ErrorCode::LIBVIPS_INIT_FAILED,
                                        QStringLiteral("ImageLoader::initialize() returned false"));
        QMessageBox::critical(
            nullptr, QStringLiteral("影图"),
            tr("Failed to initialize the image processing engine (libvips).\n"
               "Image loading, conversion, resizing and stitching will not work.\n\n"
               "Please verify the application was deployed correctly."));
    }

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

    // 切换期间屏蔽 LanguageChange：装/卸翻译器各触发一次，
    // 控件若每次都重建界面（属性面板有八个面板）会既慢又闪。
    g_languageSwitching = true;

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
    bool qtLoaded =
        m_qtTranslator->load(qtQm, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    if (!qtLoaded)
        qtLoaded =
            m_qtTranslator->load(qtQm, applicationDirPath() + QStringLiteral("/translations"));
    if (!qtLoaded)
        qtLoaded = m_qtTranslator->load(qtQm, applicationDirPath());
    if (qtLoaded) {
        installTranslator(m_qtTranslator);
    } else {
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    g_languageSwitching = false;

    // 统一补发一次 LanguageChange：此时新旧翻译器都已就位，
    // 各控件只需重译一次，界面即刻换成新语言，无需重启。
    const QList<QWidget*> topLevels = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevels)
        QCoreApplication::sendEvent(widget, new QEvent(QEvent::LanguageChange));

    QSettings settings;
    settings.setValue(QStringLiteral("language"), code);
}

QString ImageProApp::currentLanguage() const
{
    return m_currentLanguage;
}

} // namespace yingtu
