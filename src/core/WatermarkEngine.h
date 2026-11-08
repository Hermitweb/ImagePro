#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>

namespace yingtu {

enum class WatermarkType {
    Text,
    Image
};

struct WatermarkSettings {
    WatermarkType type = WatermarkType::Text;
    QString text = QStringLiteral("影图");
    QString imagePath;
    int imageScalePercent = 25; // 图片水印宽度 = 主图宽度的百分比
    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int fontSize = 24;
    QColor color = Qt::white;
    int opacity = 50;
    int rotation = 0;
    int position = 4; // center
    bool tile = false;
    int tileSpacing = 100;
    int margin = 20;
    QString outputFormat = QStringLiteral("original");
    int quality = 90;
    QString outputDir;
    QString explicitOutputDir; // 批量：若非空，直接使用该目录
    QString explicitOutputPath; // 单张：若非空，直接保存到该路径
};

class WatermarkEngine : public QObject
{
    Q_OBJECT
public:
    explicit WatermarkEngine(QObject* parent = nullptr);

    void setSettings(const WatermarkSettings& settings) { m_settings = settings; }

    /** 取消标志（可选）：置位后剩余文件跳过，已完成部分正常返回。 */
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; }

    QStringList process(const QStringList& filePaths, bool* ok = nullptr);
    static QImage preview(const QImage& source, const WatermarkSettings& settings);

signals:
    void progress(int percent);
    void finished(const QStringList& outputPaths);
    void error(const QString& message);

private:
    QString processSingle(const QString& filePath);

    WatermarkSettings m_settings;
    std::atomic<bool>* m_cancelFlag = nullptr;
    bool cancelRequested() const { return m_cancelFlag && m_cancelFlag->load(); }
};

} // namespace yingtu
