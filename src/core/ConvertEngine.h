#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>

#include <atomic>

namespace yingtu {

struct ConvertSettings {
    QString targetFormat = QStringLiteral("jpg");
    int quality = 90;
    bool keepExif = true;
    bool convertToSRgb = false;
    QString outputDir;
    QString explicitOutputDir; // 批量：若非空，转换结果保存到该目录
    QString explicitOutputPath; // 单张：若非空，直接保存到该路径
};

class ConvertEngine : public QObject
{
    Q_OBJECT
public:
    explicit ConvertEngine(QObject* parent = nullptr);

    void setSettings(const ConvertSettings& settings) { m_settings = settings; }

    /**
     * 取消标志（可选）：由调用方在进度对话框的取消按钮里置位。
     * 每个文件开工前检查，置位后剩余文件跳过，已完成部分正常返回。
     */
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; }

    QStringList process(const QStringList& filePaths, bool* ok = nullptr);
    static qint64 estimateSize(const QString& filePath, const ConvertSettings& settings);
    static qint64 estimateSize(const QImage& image, const ConvertSettings& settings);

signals:
    void progress(int percent);
    void finished(const QStringList& outputPaths);
    void error(const QString& message);

private:
    bool convertSingle(const QString& inputPath, const QString& outputPath);
    bool cancelRequested() const { return m_cancelFlag && m_cancelFlag->load(); }

    ConvertSettings m_settings;
    std::atomic<bool>* m_cancelFlag = nullptr;
};

} // namespace yingtu
