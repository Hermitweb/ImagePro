#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>

namespace yingtu {

struct ConvertSettings {
    QString targetFormat = QStringLiteral("jpg");
    int quality = 90;
    bool keepExif = true;
    bool convertToSRgb = false;
    QString outputDir;
    QString explicitOutputDir; // 批量：若非空，转换结果保存到该目录
    QString explicitOutputPath; // 单张：若非空，直接保存到该路径
    bool batchApply = true;
};

class ConvertEngine : public QObject
{
    Q_OBJECT
public:
    explicit ConvertEngine(QObject* parent = nullptr);

    void setSettings(const ConvertSettings& settings) { m_settings = settings; }

    QStringList process(const QStringList& filePaths, bool* ok = nullptr);
    static qint64 estimateSize(const QString& filePath, const ConvertSettings& settings);
    static qint64 estimateSize(const QImage& image, const ConvertSettings& settings);

signals:
    void progress(int percent);
    void finished(const QStringList& outputPaths);
    void error(const QString& message);

private:
    bool convertSingle(const QString& inputPath, const QString& outputPath);

    ConvertSettings m_settings;
};

} // namespace yingtu
