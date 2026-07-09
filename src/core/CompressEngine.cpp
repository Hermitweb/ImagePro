#include "CompressEngine.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QBuffer>
#include <QDebug>
#include <QFileInfo>

namespace yingtu {

CompressEngine::CompressEngine(QObject* parent)
    : QObject(parent)
{
}

QList<CompressResult> CompressEngine::process(const QStringList& filePaths)
{
    QList<CompressResult> results;
    int total = filePaths.size();
    for (int i = 0; i < total; ++i) {
        results.append(compressSingle(filePaths.at(i)));
        emit progress(qRound((i + 1) * 100.0 / total));
    }
    emit finished(results);
    return results;
}

CompressResult CompressEngine::compressSingle(const QString& filePath)
{
    CompressResult result;
    result.originalSize = QFileInfo(filePath).size();

    QFileInfo fi(filePath);
    QString dir = m_settings.outputDir.isEmpty() ? fi.absolutePath() : m_settings.outputDir;
    QString fmt = m_settings.outputFormat == QStringLiteral("original") ? fi.suffix() : m_settings.outputFormat;
    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    QString outputPath = FileUtils::generateUniqueOutputPath(dir, fi.completeBaseName() + QStringLiteral("_compressed"),
                                                             QStringLiteral(".") + fmt.toLower());

    bool needIterativeSize = (m_settings.mode == CompressMode::Size && m_settings.targetSize > 0);

    // Size 模式目标大小压缩优先走 libvips 流式管道，避免大图全载入内存
    if (needIterativeSize &&
        (fmt.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0 ||
         fmt.compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0)) {
        QSize target;
        if (m_settings.scalePercent < 100 && m_settings.scalePercent > 0) {
            ImageInfo info = ImageLoader::loadInfo(filePath);
            if (info.valid)
                target = QSize(info.width * m_settings.scalePercent / 100,
                               info.height * m_settings.scalePercent / 100);
        }
        if (ImageLoader::compressToTargetSize(filePath, outputPath, target,
                                              m_settings.targetSize, fmt, m_settings.quality)) {
            result.outputPath = outputPath;
            result.compressedSize = QFileInfo(outputPath).size();
            result.success = true;
            return result;
        }
    }

    // 简单缩放/质量压缩优先使用 libvips 流式处理
    bool canUseVips = (m_settings.mode != CompressMode::Size || m_settings.scalePercent <= 100);
    if (canUseVips) {
        QSize target;
        if (m_settings.scalePercent < 100 && m_settings.scalePercent > 0) {
            ImageInfo info = ImageLoader::loadInfo(filePath);
            if (info.valid)
                target = QSize(info.width * m_settings.scalePercent / 100,
                               info.height * m_settings.scalePercent / 100);
        }
        if (ImageLoader::saveThumbnail(filePath, outputPath, target, fmt, m_settings.quality)) {
            result.outputPath = outputPath;
            result.compressedSize = QFileInfo(outputPath).size();
            result.success = true;
            return result;
        }
    }

    QImage img;
    if (m_settings.scalePercent < 100 && m_settings.scalePercent > 0) {
        // 优先使用 libvips thumbnail 流式缩放，避免把大图全载入内存
        ImageInfo info = ImageLoader::loadInfo(filePath);
        if (info.valid) {
            QSize target(info.width * m_settings.scalePercent / 100,
                         info.height * m_settings.scalePercent / 100);
            img = ImageLoader::loadPreview(filePath, target);
        }
    }
    if (img.isNull())
        img = ImageLoader::loadImage(filePath);
    if (img.isNull()) {
        result.errorString = tr("Failed to load image");
        return result;
    }

    QImage out = preview(img, m_settings);
    int q = m_settings.quality;
    if (needIterativeSize) {
        // 简单迭代逼近目标大小
        for (int attempt = 0; attempt < 8; ++attempt) {
            QByteArray data;
            QBuffer buffer(&data);
            buffer.open(QIODevice::WriteOnly);
            out.save(&buffer, fmt.toUpper().toUtf8(), q);
            buffer.close();
            if (qAbs(data.size() - m_settings.targetSize) < m_settings.targetSize * 0.1 || q <= 10 || q >= 95)
                break;
            if (data.size() > m_settings.targetSize)
                q = qMax(10, q - 10);
            else
                q = qMin(95, q + 5);
        }
    }

    bool ok = ImageLoader::saveImage(out, outputPath, fmt, q);
    if (!ok) {
        qDebug() << "DEBUG save failed:" << out.size() << out.format() << outputPath << fmt << q;
        result.errorString = tr("Failed to save compressed image");
        return result;
    }

    result.outputPath = outputPath;
    result.compressedSize = QFileInfo(outputPath).size();
    result.success = true;
    return result;
}

QImage CompressEngine::preview(const QImage& source, const CompressSettings& settings)
{
    QImage img = source;
    if (settings.mode == CompressMode::Size || settings.mode == CompressMode::Smart) {
        if (settings.scalePercent < 100 && settings.scalePercent > 0) {
            img = img.scaled(source.width() * settings.scalePercent / 100,
                             source.height() * settings.scalePercent / 100,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    return img;
}

qint64 CompressEngine::estimateSize(const QImage& image, const CompressSettings& settings)
{
    if (image.isNull())
        return 0;

    QImage out = preview(image, settings);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);

    QString fmt = settings.outputFormat == QStringLiteral("original") ? QStringLiteral("jpeg") : settings.outputFormat;
    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    int q = settings.quality;
    if (settings.mode == CompressMode::Size && settings.targetSize > 0) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            data.clear();
            QBuffer inner(&data);
            inner.open(QIODevice::WriteOnly);
            out.save(&inner, fmt.toUpper().toUtf8(), q);
            inner.close();
            if (qAbs(data.size() - settings.targetSize) < settings.targetSize * 0.1 || q <= 10 || q >= 95)
                break;
            if (data.size() > settings.targetSize)
                q = qMax(10, q - 10);
            else
                q = qMin(95, q + 5);
        }
    } else {
        out.save(&buffer, fmt.toUpper().toUtf8(), q);
    }
    buffer.close();
    return data.size();
}

} // namespace yingtu
