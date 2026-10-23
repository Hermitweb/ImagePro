#include "ResizeEngine.h"
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QFileInfo>

namespace yingtu {

ResizeEngine::ResizeEngine(QObject* parent) : QObject(parent) {}

QStringList ResizeEngine::process(const QStringList& filePaths, bool* ok)
{
    if (ok)
        *ok = false;
    if (filePaths.isEmpty()) {
        ErrorHandler::instance().report(ErrorCode::EMPTY_FOLDER, tr("No images to resize"));
        emit error(tr("No images to resize"));
        return QStringList();
    }

    QStringList outputPaths;
    int total = filePaths.size();
    for (int i = 0; i < total; ++i) {
        const QString& path = filePaths.at(i);
        if (!QFileInfo::exists(path)) {
            ErrorHandler::instance().report(ErrorCode::FILE_NOT_FOUND, path);
            emit progress(qRound((i + 1) * 100.0 / total));
            continue;
        }

        QString out = processSingle(path);
        if (!out.isEmpty()) {
            outputPaths.append(out);
        } else {
            ErrorHandler::instance().report(ErrorCode::WRITE_FAILED, path);
        }
        emit progress(qRound((i + 1) * 100.0 / total));
    }

    if (ok)
        *ok = !outputPaths.isEmpty();
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

    // 尺寸必须为正：百分比为 0、预设目标非法、或宽高比缩放取整都可能算出 0 或负数。
    // 若不钳制，libvips 路径会把「空尺寸」当作不缩放，静默按原尺寸复制文件并报成功，
    // 而 QImage 路径返回空图并报失败——两条路径行为不一致且会误导用户。
    w = qMax(1, w);
    h = qMax(1, h);
    return QSize(w, h);
}

QSize ResizeEngine::outputSize(const QImage& source, const ResizeSettings& settings)
{
    return outputSize(source.size(), settings);
}

QString ResizeEngine::processSingle(const QString& filePath)
{
    QFileInfo fi(filePath);
    QString fmt = m_settings.outputFormat == QStringLiteral("original") ? fi.suffix()
                                                                        : m_settings.outputFormat;
    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    QString outputPath;
    // reserved 表示该路径由本函数原子占位（而非用户显式指定），
    // 写入失败时才可以安全地清理占位文件。
    bool reserved = false;
    if (!m_settings.explicitOutputPath.isEmpty()) {
        outputPath = m_settings.explicitOutputPath;
    } else {
        QString dir = m_settings.explicitOutputDir;
        if (dir.isEmpty())
            dir = m_settings.outputDir.isEmpty() ? fi.absolutePath() : m_settings.outputDir;
        outputPath = FileUtils::reserveUniqueOutputPath(
            dir, fi.completeBaseName() + QStringLiteral("_resized"),
            QStringLiteral(".") + fmt.toLower());
        reserved = !outputPath.isEmpty();
    }
    if (outputPath.isEmpty())
        return QString();

    // 使用 libvips 流式缩放，避免全图加载；包括拉伸、放大与非等比场景
    ImageInfo info = ImageLoader::loadInfo(filePath);
    if (info.valid) {
        QSize target = outputSize(QSize(info.width, info.height), m_settings);
        int kernel = 1; // linear
        switch (m_settings.interpolation) {
        case Interpolation::Nearest:
            kernel = 0;
            break;
        case Interpolation::Bilinear:
            kernel = 1;
            break;
        case Interpolation::Bicubic:
            kernel = 2;
            break;
        case Interpolation::Lanczos:
            kernel = 3;
            break;
        }
        if (ImageLoader::resizeFile(filePath, outputPath, target, m_settings.lockAspectRatio,
                                    kernel, fmt, m_settings.quality)) {
            return outputPath;
        }
    }

    // 回退到 QImage
    QImage img = ImageLoader::loadImage(filePath);
    if (img.isNull()) {
        if (reserved)
            FileUtils::discardReservedPath(outputPath);
        return QString();
    }

    QImage out = resize(img, m_settings);
    bool ok = ImageLoader::saveImage(out, outputPath, fmt, m_settings.quality);
    if (ok)
        return outputPath;
    // 写入失败：清理占位文件，避免留下 0 字节的「假成功」产物。
    if (reserved)
        FileUtils::discardReservedPath(outputPath);
    return QString();
}

} // namespace yingtu
