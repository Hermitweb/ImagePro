#include "FileUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>

namespace yingtu {

QString FileUtils::formatFileSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("0 B");
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

QStringList FileUtils::supportedImageFormats()
{
    QStringList formats;
    for (const QByteArray& fmt : QImageReader::supportedImageFormats()) {
        QString f = QString::fromLatin1(fmt).toLower();
        if (!formats.contains(f))
            formats.append(f);
    }
    // 常见扩展名兜底
    for (const QString& f : QStringList() << QStringLiteral("png") << QStringLiteral("jpg")
                                          << QStringLiteral("jpeg") << QStringLiteral("bmp")
                                          << QStringLiteral("gif") << QStringLiteral("webp")
                                          << QStringLiteral("tiff") << QStringLiteral("tif")) {
        if (!formats.contains(f))
            formats.append(f);
    }
    return formats;
}

QString FileUtils::imageFileFilter()
{
    QStringList patterns;
    for (const QString& fmt : supportedImageFormats())
        patterns.append(QStringLiteral("*.%1").arg(fmt));
    return QStringLiteral("Images (%1);;All Files (*)").arg(patterns.join(QStringLiteral(" ")));
}

bool FileUtils::isSupportedImage(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return supportedImageFormats().contains(suffix);
}

QString FileUtils::generateUniqueOutputPath(const QString& dir, const QString& baseName, const QString& suffix)
{
    QString cleanBase = baseName;
    QString ext = suffix;
    if (ext.startsWith(QStringLiteral(".")))
        ext = ext.mid(1);

    QString outputDir = dir;
    if (outputDir.isEmpty())
        outputDir = QDir::currentPath();

    QDir d(outputDir);
    d.mkpath(outputDir);

    QString candidate = d.absoluteFilePath(cleanBase + QStringLiteral(".") + ext);
    if (!QFile::exists(candidate))
        return candidate;

    for (int i = 1; i < 10000; ++i) {
        candidate = d.absoluteFilePath(QStringLiteral("%1_%2.%3").arg(cleanBase).arg(i).arg(ext));
        if (!QFile::exists(candidate))
            return candidate;
    }
    return candidate;
}

bool FileUtils::saveImage(const QImage& image, const QString& path, const QString& format, int quality)
{
    if (image.isNull() || path.isEmpty())
        return false;
    QString fmt = format;
    if (fmt.isEmpty())
        fmt = QFileInfo(path).suffix();
    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    QImageWriter writer(path, fmt.toUpper().toUtf8());
    if (quality >= 0)
        writer.setQuality(quality);
    return writer.write(image);
}

QPixmap FileUtils::createPlaceholderThumbnail(int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setPen(Qt::gray);
    painter.drawRect(0, 0, size - 1, size - 1);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QStringLiteral("?"));
    return pix;
}

} // namespace yingtu
