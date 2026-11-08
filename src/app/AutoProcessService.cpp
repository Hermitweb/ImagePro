#include "AutoProcessService.h"
#include "core/CompressEngine.h"
#include "core/ConvertEngine.h"
#include "core/ResizeEngine.h"
#include "utils/AppPaths.h"
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include "utils/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

namespace yingtu {

namespace {

// 轮询间隔：兜底「服务停止期间落盘的文件」与扫描间隙内的新文件。
constexpr int kPollIntervalMs = 5000;

// 停止检查的睡眠分片：保证 250ms 内响应停止请求，同时维持 5s 轮询粒度。
constexpr int kStopCheckIntervalMs = 250;

// 同一文件连续处理失败的上限：超过后放弃，等文件被替换（日志会记录）。
constexpr int kMaxProcessAttempts = 3;

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

int AutoProcessService::run()
{
    // 配置开关：enabled=false 时空转退出，不做任何处理。
    // 此前该字段被加载和保存、却从未被检查——用户写 false 期望停用，
    // 服务却照常监视、照常删源文件，配置承诺与行为相反。
    if (!m_config.enabled) {
        Logger::info(QStringLiteral("自动处理已在配置中禁用（enabled=false），服务空转退出"),
                     QStringLiteral("Service"));
        return 0;
    }

    if (!QDir().mkpath(m_config.watchDir)) {
        // 目录创建失败不退出：scanOnce 每轮都会重查目录是否存在，
        // 目录（如网络盘）稍后出现时自动开始工作。
        Logger::error(QStringLiteral("无法创建监视目录：%1（目录出现后将自动开始处理）")
                          .arg(m_config.watchDir),
                      QStringLiteral("Service"));
    }

    scanOnce();
    Logger::info(QStringLiteral("自动处理服务已启动，监视目录：%1").arg(m_config.watchDir),
                 QStringLiteral("Service"));

    // 主循环运行在 SCM 的 ServiceMain 线程上，没有事件循环；
    // 用分片睡眠轮询，既保证轮询粒度，又能在停止请求到来时快速退出。
    while (!m_stopRequested) {
        for (int waited = 0; waited < kPollIntervalMs && !m_stopRequested;
             waited += kStopCheckIntervalMs)
            QThread::msleep(kStopCheckIntervalMs);
        if (!m_stopRequested)
            scanOnce();
    }

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
    QSet<QString> present;
    for (const QFileInfo& info : files) {
        if (isTemporaryFile(info.fileName()))
            continue;
        if (!FileUtils::isSupportedImage(info.absoluteFilePath()))
            continue;

        const QString key = info.absoluteFilePath();
        present.insert(key);

        // 连续失败太多次的文件不再尝试（避免死循环刷日志），等它被替换。
        if (m_failures.value(key, 0) >= kMaxProcessAttempts)
            continue;

        // 只处理「尺寸稳定」的文件：上一轮与本轮大小一致才认为落盘完成。
        // 否则正在复制中的大文件会被立即处理——扩展名合法 → 加载失败或
        // 产出损坏结果 → 且此前的「先标记再处理」会让它永远不再被重试。
        const qint64 size = info.size();
        if (size <= 0)
            continue;
        const auto sizeIt = m_sizes.constFind(key);
        if (sizeIt == m_sizes.constEnd() || sizeIt.value() != size) {
            m_sizes.insert(key, size);
            continue;
        }

        if (m_seen.contains(key))
            continue; // 已成功处理过，不重复处理
        m_seen.insert(key);
        if (processFile(key)) {
            Logger::info(QStringLiteral("已自动处理：%1").arg(key), QStringLiteral("Service"));
        } else {
            // 失败允许有限次重试（如杀毒软件短暂锁住新文件），而不是永久跳过。
            m_seen.remove(key);
            const int attempts = m_failures.value(key, 0) + 1;
            m_failures.insert(key, attempts);
            if (attempts >= kMaxProcessAttempts)
                Logger::warning(
                    QStringLiteral("自动处理连续失败 %1 次，放弃该文件：%2").arg(attempts).arg(key),
                    QStringLiteral("Service"));
        }
    }

    // 清理已消失文件的记录：长期运行不积累内存，同名新文件也能重新被处理。
    m_seen.intersect(present);
    for (auto it = m_sizes.begin(); it != m_sizes.end();) {
        if (!present.contains(it.key()))
            it = m_sizes.erase(it);
        else
            ++it;
    }
    for (auto it = m_failures.begin(); it != m_failures.end();) {
        if (!present.contains(it.key()))
            it = m_failures.erase(it);
        else
            ++it;
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
