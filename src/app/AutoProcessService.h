#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

namespace yingtu {

/**
 * 自动处理服务：监视一个目录，把新出现的图片按配置处理后输出到另一个目录。
 *
 * 这是 Windows 服务模式下的实际功能载体。配置放在应用配置目录下的
 * `service.json`，结构：
 * {
 *   "enabled": true,
 *   "watchDir":  "D:\\待处理",
 *   "outputDir": "D:\\已处理",
 *   "action":    "webp",        // webp | jpeg | compress | resize
 *   "quality":  82,
 *   "maxWidth": 0,             // 0 表示不限制
 *   "deleteSource": false
 * }
 *
 * 服务用「事件 + 轮询」双通道发现新文件：QFileSystemWatcher 负责实时性，
 * 轮询负责补上服务停止期间落盘的文件。
 */
class AutoProcessService : public QObject
{
    Q_OBJECT
public:
    explicit AutoProcessService(QObject* parent = nullptr);

    /** 运行主循环，直到收到停止请求。返回进程退出码。 */
    int run();

    /** 请求停止主循环（由服务控制处理器调用）。 */
    void requestStop();

    /** 配置文件路径。 */
    static QString configFilePath();

    /** 默认监视目录（配置未指定时）。 */
    static QString defaultWatchDir();

private:
    struct Config {
        bool enabled = true;
        QString watchDir;
        QString outputDir;
        QString action = QStringLiteral("webp");
        int quality = 82;
        int maxWidth = 0;
        bool deleteSource = false;
    };

    Config loadConfig() const;
    void saveConfig(const Config& config) const;

    void setupWatcher();
    void scanOnce();

    /** 处理单个文件；返回是否成功产出结果。 */
    bool processFile(const QString& path);

    QString outputPathFor(const QString& path) const;

    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_timer = nullptr;
    QSet<QString> m_seen;
    Config m_config;
    bool m_stopRequested = false;
};

} // namespace yingtu
