#include "ResizeEngine.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QFileInfo>

namespace yingtu {

ResizeEngine::ResizeEngine(QObject* parent)
    : QObject(parent)
{
}

QStringList ResizeEngine::process(const QStringList& filePaths, bool* ok)
{
    if (ok) *ok = false;
    if (filePaths.isEmpty()) {
        emit error(tr("No images to resize"));
        return QStringList();
    }

    QStringList outputPaths;
    int total = filePaths.size();
    for (int i = 0; i < total; ++i) {
        QString out = processSingle(filePaths.at(i));
        if (!out.isEmpty())
            outputPaths.append(out);
        emit progress(qRound((i + 1) * 100.0 / total));
    }

    if (ok) *ok = !outputPaths.isEmpty();
    emit finished(outputPaths);
    return outputPaths;
}

QImage ResizeEngine::resize(const QImage& source, const ResizeSettings& settings)
{
    QSize target = outputSize(source, settings);
    if (!target.isValid() || target == source.size())
        return source.copy();

    Qt::TransformationMode mode = Qt::SmoothTransformation;
    if (settings.interpolation == Interpolation::Nearest)
        mode = Qt::FastTransformation;

    return source.scaled(target, Qt::IgnoreAspectRatio, mode);
}

QSize ResizeEngine::outputSize(const QSize& sourceSize, const ResizeSettings& settings)
{
    int w = sourceSize.width();
    int h = sourceSize.height();
    switch (settings.mode) {
    case ResizeMode::Percentage:
        w = sourceSize.width() * settings.percentage / 100;
        h = sourceSize.height() * settings.percentage / 100;
        break;
    case ResizeMode::Pixel:
        if (settings.targetWidth > 0 && settings.targetHeight > 0) {
            w = settings.targetWidth;
            h = settings.targetHeight;
            if (settings.lockAspectRatio) {
                QSize s = sourceSize.scaled(w, h, Qt::KeepAspectRatio);
                w = s.width();
                h = s.height();
            }
        }
        break;
    case ResizeMode::Preset:
        // Preset 模式下 targetWidth/Height 已提前写入 settings
        w = settings.targetWidth;
        h = settings.targetHeight;
        if (settings.lockAspectRatio) {
            QSize s = sourceSize.scaled(w, h, Qt::KeepAspectRatio);
            w = s.width();
            h = s.height();
        }
        break;
    }

    if (settings.fitWithinOriginal) {
        w = qMin(w, sourceSize.width());
        h = qMin(h, sourceSize.height());
        if (settings.lockAspectRatio && (w != sourceSize.width() || h != sourceSize.height())) {
            QSize s = sourceSize.scaled(w, h, Qt::KeepAspectRatio);
            w = s.width();
            h = s.height();
        }
    }
    return QSize(w, h);
}

QSize ResizeEngine::outputSize(const QImage& source, const ResizeSettings& settings)
{
    return outputSize(source.size(), settings);
}

QString ResizeEngine::processSingle(const QString& filePath)
{
    QFileInfo fi(filePath);
    QString dir = m_settings.outputDir.isEmpty() ? fi.absolutePath() : m_settings.outputDir;
    QString fmt = m_settings.outputFormat == QStringLiteral("original") ? fi.suffix() : m_settings.outputFormat;
    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    QString outputPath = FileUtils::generateUniqueOutputPath(dir, fi.completeBaseName() + QStringLiteral("_resized"),
                                                             QStringLiteral(".") + fmt.toLower());

    // 使用 libvips 流式缩放，避免全图加载；包括拉伸、放大与非等比场景
    ImageInfo info = ImageLoader::loadInfo(filePath);
    if (info.valid) {
        QSize target = outputSize(QSize(info.width, info.height), m_settings);
        int kernel = 1; // linear
        switch (m_settings.interpolation) {
        case Interpolation::Nearest: kernel = 0; break;
        case Interpolation::Bilinear: kernel = 1; break;
        case Interpolation::Bicubic: kernel = 2; break;
        case Interpolation::Lanczos: kernel = 3; break;
        }
        if (ImageLoader::resizeFile(filePath, outputPath, target, m_settings.lockAspectRatio,
                                    kernel, fmt, m_settings.quality)) {
            return outputPath;
        }
    }

    // 回退到 QImage
    QImage img = ImageLoader::loadImage(filePath);
    if (img.isNull())
        return QString();

    QImage out = resize(img, m_settings);
    bool ok = ImageLoader::saveImage(out, outputPath, fmt, m_settings.quality);
    return ok ? outputPath : QString();
}

} // namespace yingtu
