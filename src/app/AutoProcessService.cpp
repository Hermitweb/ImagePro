#include "AutoProcessService.h"
#include "core/CompressEngine.h"
#include "core/ConvertEngine.h"
#include "core/ResizeEngine.h"
#include "utils/AppPaths.h"
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include "utils/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>

namespace yingtu {

namespace {

// 轮询间隔：兜底「服务停止期间落盘的文件」与文件系统事件的漏报。
constexpr int kPollIntervalMs = 5000;

// 忽略的临时文件：处理工具写出的中间文件不应再次被当成输入。
bool isTemporaryFile(const QString& name)
{
    return name.endsWith(QStringLiteral(".tmp"), Qt::CaseInsensitive) ||
           name.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive) ||
           name.startsWith(QStringLiteral("~$"));
}

} // namespace

AutoProcessService::AutoProcessService(QObject* parent) : QObject(parent)
{
    m_config = loadConfig();

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString&) { scanOnce(); });

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this]() { scanOnce(); });
}

QString AutoProcessService::configFilePath()
{
    return appConfigDir() + QStringLiteral("/service.json");
}

QString AutoProcessService::defaultWatchDir()
{
    return appConfigDir() + QStringLiteral("/auto-process");
}

AutoProcessService::Config AutoProcessService::loadConfig() const
{
    Config config;
    config.watchDir = defaultWatchDir();
    config.outputDir = config.watchDir + QStringLiteral("/output");

    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return config;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    config.enabled = root.value(QStringLiteral("enabled")).toBool(true);
    config.watchDir = root.value(QStringLiteral("watchDir")).toString(config.watchDir);
    config.outputDir = root.value(QStringLiteral("outputDir")).toString(config.outputDir);
    config.action = root.value(QStringLiteral("action")).toString(config.action);
    config.quality = root.value(QStringLiteral("quality")).toInt(config.quality);
    config.maxWidth = root.value(QStringLiteral("maxWidth")).toInt(config.maxWidth);
    config.deleteSource = root.value(QStringLiteral("deleteSource")).toBool(false);
    return config;
}

void AutoProcessService::saveConfig(const Config& config) const
{
    QJsonObject root;
    root[QStringLiteral("enabled")] = config.enabled;
    root[QStringLiteral("watchDir")] = config.watchDir;
    root[QStringLiteral("outputDir")] = config.outputDir;
    root[QStringLiteral("action")] = config.action;
    root[QStringLiteral("quality")] = config.quality;
    root[QStringLiteral("maxWidth")] = config.maxWidth;
    root[QStringLiteral("deleteSource")] = config.deleteSource;

    QDir().mkpath(appConfigDir());
    QFile file(configFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void AutoProcessService::setupWatcher()
{
    if (!m_watcher)
        return;

    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);

    if (!QDir().mkpath(m_config.watchDir)) {
        Logger::error(QStringLiteral("无法创建监视目录：%1").arg(m_config.watchDir),
                      QStringLiteral("Service"));
        return;
    }
    m_watcher->addPath(m_config.watchDir);
}

int AutoProcessService::run()
{
    setupWatcher();
    m_timer->start();
    scanOnce();

    Logger::info(QStringLiteral("自动处理服务已启动，监视目录：%1").arg(m_config.watchDir),
                 QStringLiteral("Service"));

    while (!m_stopRequested) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        QThread::msleep(50);
    }

    m_timer->stop();
    Logger::info(QStringLiteral("自动处理服务已停止"), QStringLiteral("Service"));
    return 0;
}

void AutoProcessService::requestStop()
{
    m_stopRequested = true;
}

void AutoProcessService::scanOnce()
{
    if (m_config.watchDir.isEmpty())
        return;

    QDir dir(m_config.watchDir);
    if (!dir.exists())
        return;

    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable);
    for (const QFileInfo& info : files) {
        if (isTemporaryFile(info.fileName()))
            continue;
        if (!FileUtils::isSupportedImage(info.absoluteFilePath()))
            continue;

        const QString key = info.absoluteFilePath();
        if (m_seen.contains(key))
            continue;

        // 先标记再处理：处理失败不重复尝试，避免死循环刷日志。
        m_seen.insert(key);
        if (processFile(key))
            Logger::info(QStringLiteral("已自动处理：%1").arg(key), QStringLiteral("Service"));
    }
}

QString AutoProcessService::outputPathFor(const QString& path) const
{
    const QFileInfo info(path);
    const QString action = m_config.action;

    QString suffix = QStringLiteral("webp");
    if (action == QStringLiteral("jpeg")) {
        suffix = QStringLiteral("jpg");
    } else if (action == QStringLiteral("compress") || action == QStringLiteral("resize")) {
        // 压缩与缩放都保持原扩展名：输出格式由各自的处理流程决定。
        suffix = info.suffix();
    }

    QDir().mkpath(m_config.outputDir);
    return QDir(m_config.outputDir)
        .filePath(QStringLiteral("%1.%2").arg(info.completeBaseName(), suffix));
}

bool AutoProcessService::processFile(const QString& path)
{
    const QString outputPath = outputPathFor(path);
    if (outputPath.isEmpty())
        return false;

    bool ok = false;
    if (m_config.action == QStringLiteral("compress")) {
        CompressSettings settings;
        settings.mode = CompressMode::Quality;
        settings.quality = m_config.quality;
        settings.outputFormat = QStringLiteral("original");
        settings.outputDir = QFileInfo(outputPath).absolutePath();
        settings.explicitOutputPath = outputPath;

        CompressEngine engine;
        engine.setSettings(settings);
        const QList<CompressResult> results = engine.process(QStringList() << path);
        ok = !results.isEmpty() && results.first().success;
    } else if (m_config.action == QStringLiteral("resize")) {
        ResizeSettings settings;
        settings.mode = ResizeMode::Percentage;
        settings.percentage = 100;
        settings.outputFormat = QFileInfo(outputPath).suffix();
        settings.outputDir = QFileInfo(outputPath).absolutePath();
        settings.explicitOutputPath = outputPath;
        if (m_config.maxWidth > 0) {
            settings.mode = ResizeMode::Pixel;
            settings.targetWidth = m_config.maxWidth;
            settings.targetHeight = 0;
        }

        ResizeEngine engine;
        engine.setSettings(settings);
        ok = !engine.process(QStringList() << path, &ok).isEmpty() && ok;
    } else {
        ConvertSettings settings;
        settings.targetFormat = m_config.action == QStringLiteral("jpeg") ? QStringLiteral("jpg")
                                                                          : QStringLiteral("webp");
        settings.quality = m_config.quality;
        settings.outputDir = QFileInfo(outputPath).absolutePath();
        settings.explicitOutputPath = outputPath;

        ConvertEngine engine;
        engine.setSettings(settings);
        const QStringList outputs = engine.process(QStringList() << path, &ok);
        ok = ok && !outputs.isEmpty();
    }

    if (!ok) {
        ErrorHandler::instance().report(ErrorCode::WRITE_FAILED,
                                        QStringLiteral("自动处理失败：%1").arg(path));
        return false;
    }

    if (m_config.deleteSource)
        QFile::remove(path);

    return true;
}

} // namespace yingtu
