#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>

namespace yingtu {

enum class CompressMode {
    Quality,
    Size,
    Smart
};

struct CompressSettings {
    CompressMode mode = CompressMode::Quality;
    int strength = 50; // 0~100
    qint64 targetSize = 0; // bytes, 0 means not specified
    QString outputFormat = QStringLiteral("original");
    int quality = 80;
    int scalePercent = 100;
    bool showOriginal = false;
    QString outputDir;
    QString explicitOutputDir; // 批量：若非空，直接使用该目录
    QString explicitOutputPath; // 单张：若非空，直接保存到该路径
};

struct CompressResult {
    QString outputPath;
    qint64 originalSize = 0;
    qint64 compressedSize = 0;
    bool success = false;
    QString errorString;
};

class CompressEngine : public QObject
{
    Q_OBJECT
public:
    explicit CompressEngine(QObject* parent = nullptr);

    void setSettings(const CompressSettings& settings) { m_settings = settings; }

    QList<CompressResult> process(const QStringList& filePaths);
    static QImage preview(const QImage& source, const CompressSettings& settings);
    static qint64 estimateSize(const QImage& image, const CompressSettings& settings);

signals:
    void progress(int percent);
    void finished(const QList<CompressResult>& results);

private:
    CompressResult compressSingle(const QString& filePath);

    CompressSettings m_settings;
};

} // namespace yingtu
