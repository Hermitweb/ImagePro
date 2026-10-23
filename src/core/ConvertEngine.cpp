#include "ConvertEngine.h"
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace yingtu {

static QByteArray readJpegExif(const QByteArray& data)
{
    if (data.size() < 4)
        return QByteArray();
    int i = 2; // skip SOI (FFD8)
    while (i < data.size() - 3) {
        quint8 markerByte = static_cast<quint8>(data.at(i));
        if (markerByte != 0xFF) {
            // Invalid marker, stop scanning
            break;
        }
        quint8 marker = static_cast<quint8>(data.at(i + 1));
        if (marker == 0xD9 || marker == 0xDA) // EOI or SOS
            break;
        if (marker == 0xE1) {
            // 用无符号量做移位与按位或：quint8 提升为 int 后参与位运算，
            // 会对有符号整数做按位操作（实现定义行为，且 clang-tidy 会告警）。
            const int len = static_cast<int>(
                (static_cast<unsigned>(static_cast<quint8>(data.at(i + 2))) << 8u) |
                static_cast<unsigned>(static_cast<quint8>(data.at(i + 3))));
            if (i + 2 + len <= data.size())
                return data.mid(i, len + 2);
            break;
        }
        if (marker == 0x00) { // escaped 0xFF
            ++i;
            continue;
        }
        const int len =
            static_cast<int>((static_cast<unsigned>(static_cast<quint8>(data.at(i + 2))) << 8u) |
                             static_cast<unsigned>(static_cast<quint8>(data.at(i + 3))));
        i += 2 + len;
    }
    return QByteArray();
}

static bool insertJpegExif(const QString& path, const QByteArray& exif)
{
    if (exif.isEmpty())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 2 || static_cast<quint8>(data.at(0)) != 0xFF ||
        static_cast<quint8>(data.at(1)) != 0xD8)
        return false;

    QByteArray out;
    out.append(data.left(2));
    out.append(exif);
    out.append(data.mid(2));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(out);
    file.close();
    return true;
}

ConvertEngine::ConvertEngine(QObject* parent) : QObject(parent) {}

QStringList ConvertEngine::process(const QStringList& filePaths, bool* ok)
{
    if (ok)
        *ok = false;
    if (filePaths.isEmpty()) {
        ErrorHandler::instance().report(ErrorCode::EMPTY_FOLDER, tr("No images to convert"));
        emit error(tr("No images to convert"));
        return QStringList();
    }

    QStringList outputPaths;
    int total = filePaths.size();
    for (int i = 0; i < total; ++i) {
        const QString& path = filePaths.at(i);
        QFileInfo fi(path);

        if (!fi.exists()) {
            ErrorHandler::instance().report(ErrorCode::FILE_NOT_FOUND, path);
            emit progress(qRound((i + 1) * 100.0 / total));
            continue;
        }

        QString outputPath;
        // reserved 表示该路径由本函数原子占位（而非用户显式指定），
        // 写入失败时才可以安全地清理占位文件。
        bool reserved = false;
        if (!m_settings.explicitOutputPath.isEmpty() && filePaths.size() == 1) {
            outputPath = m_settings.explicitOutputPath;
        } else {
            QString dir = m_settings.explicitOutputDir;
            if (dir.isEmpty())
                dir = m_settings.outputDir.isEmpty() ? fi.absolutePath() : m_settings.outputDir;
            QString baseName = fi.completeBaseName();
            QString suffix = QStringLiteral(".") + m_settings.targetFormat.toLower();
            outputPath = FileUtils::reserveUniqueOutputPath(dir, baseName, suffix);
            reserved = !outputPath.isEmpty();
        }

        if (outputPath.isEmpty()) {
            ErrorHandler::instance().report(ErrorCode::PERMISSION_DENIED, fi.absolutePath());
        } else if (convertSingle(path, outputPath)) {
            outputPaths.append(outputPath);
        } else {
            // 写入失败：清理占位文件，避免留下 0 字节的「假成功」产物。
            if (reserved)
                FileUtils::discardReservedPath(outputPath);
            ErrorHandler::instance().report(ErrorCode::WRITE_FAILED, path);
        }

        emit progress(qRound((i + 1) * 100.0 / total));
    }

    if (ok)
        *ok = !outputPaths.isEmpty();
    emit finished(outputPaths);
    return outputPaths;
}

bool ConvertEngine::convertSingle(const QString& inputPath, const QString& outputPath)
{
    QString fmt = m_settings.targetFormat.toUpper();
    if (fmt == QStringLiteral("JPG"))
        fmt = QStringLiteral("JPEG");

    bool saved = false;
    if (!m_settings.convertToSRgb) {
        // 优先使用 libvips 流式转换，避免全图加载
        saved = ImageLoader::convertFile(inputPath, outputPath, fmt, m_settings.quality);
    } else {
        // 使用 libvips 转换到 sRGB 色彩空间
        saved = ImageLoader::convertToSRgbFile(inputPath, outputPath, fmt, m_settings.quality);
        if (!saved) {
            // 回退到 QImage
            QImage img = ImageLoader::loadImage(inputPath);
            if (img.isNull())
                return false;

            if (img.colorSpace().isValid()) {
                img = img.convertToFormat(QImage::Format_RGB32);
                img.setColorSpace(QColorSpace::SRgb);
            }

            saved = ImageLoader::saveImage(img, outputPath, fmt, m_settings.quality);
        }
    }

    if (saved && m_settings.keepExif) {
        QFile in(inputPath);
        if (in.open(QIODevice::ReadOnly)) {
            QByteArray exif = readJpegExif(in.readAll());
            if (!exif.isEmpty())
                insertJpegExif(outputPath, exif);
        }
    }
    return saved;
}

qint64 ConvertEngine::estimateSize(const QString& filePath, const ConvertSettings& settings)
{
    QImage img = ImageLoader::loadImage(filePath);
    if (img.isNull())
        return 0;
    return estimateSize(img, settings);
}

qint64 ConvertEngine::estimateSize(const QImage& image, const ConvertSettings& settings)
{
    if (image.isNull())
        return 0;

    // 粗略估算：像素数 × 每像素字节数 × 压缩因子
    qint64 pixels = qint64(image.width()) * image.height();
    if (settings.targetFormat.compare(QStringLiteral("png"), Qt::CaseInsensitive) == 0)
        return qRound64(pixels * 4 * 0.3); // PNG 大致压缩率
    if (settings.targetFormat.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0 ||
        settings.targetFormat.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0)
        return qRound64(pixels * 3 * (settings.quality / 200.0 + 0.05));
    if (settings.targetFormat.compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0)
        return qRound64(pixels * 3 * (settings.quality / 250.0 + 0.03));
    if (settings.targetFormat.compare(QStringLiteral("gif"), Qt::CaseInsensitive) == 0)
        return qRound64(pixels * 3 * 0.2);
    if (settings.targetFormat.compare(QStringLiteral("bmp"), Qt::CaseInsensitive) == 0)
        return pixels * 3;
    if (settings.targetFormat.compare(QStringLiteral("tiff"), Qt::CaseInsensitive) == 0 ||
        settings.targetFormat.compare(QStringLiteral("tif"), Qt::CaseInsensitive) == 0)
        return qRound64(pixels * 4 * 0.5);
    return pixels * 4;
}

} // namespace yingtu
