#pragma once

#include <QImage>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace yingtu {

class FileUtils
{
public:
    static QString formatFileSize(qint64 bytes);
    static QStringList supportedImageFormats();
    static QString imageFileFilter();
    static bool isSupportedImage(const QString& filePath);
    static QString generateUniqueOutputPath(const QString& dir, const QString& baseName, const QString& suffix);

    static bool saveImage(const QImage& image, const QString& path, const QString& format = QString(), int quality = -1);
    static QPixmap createPlaceholderThumbnail(int size);
};

} // namespace yingtu
