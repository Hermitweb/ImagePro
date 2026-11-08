#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>

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
 * 服务用「轮询」发现新文件：每 5 秒扫描一次监视目录，文件大小连续两轮
 * 一致才认为落盘完成（正在复制中的大文件不会被立即处理而产出损坏结果）。
 *
 * 为什么不用 QFileSystemWatcher + QTimer 双通道：run() 由 SCM 在
 * ServiceMain 线程上调用，而 QObject 的事件投递只发生在对象所属线程的
 * 事件循环里——服务模式下事件循环从未运转，定时器与目录事件全部不可达。
 * 轮询是唯一可靠的方式，且 `enabled: false` 会直接停用处理逻辑。
 */
class AutoProcessService : public QObject
{
    Q_OBJECT
public:
    explicit AutoProcessService(QObject* parent = nullptr);

    /** 运行主循环，直到收到停止请求。返回进程退出码。 */
    int run();

    /** 请求停止主循环（由服务控制处理器调用，跨线程安全）。 */
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

    void scanOnce();

    /** 处理单个文件；返回是否成功产出结果。 */
    bool processFile(const QString& path);

    QString outputPathFor(const QString& path) const;

    QSet<QString> m_seen; // 已成功处理过的文件
    QHash<QString, qint64> m_sizes; // 上一轮观测到的文件大小（判断落盘稳定）
    QHash<QString, int> m_failures; // 连续失败次数（有上限，避免死循环刷日志）
    Config m_config;
    std::atomic<bool> m_stopRequested{false};
};

} // namespace yingtu
