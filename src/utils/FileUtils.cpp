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
    // 用 qint64 常量而不是 1024 * 1024：后者在 int 中求值，
    // 1 GB 的阈值会溢出到负数，导致大文件被误判为「小于 1 MB」。
    constexpr qint64 kKB = 1024;
    constexpr qint64 kMB = kKB * 1024;
    constexpr qint64 kGB = kMB * 1024;

    if (bytes < 0)
        return QStringLiteral("0 B");
    if (bytes < kKB)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < kMB)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < kGB)
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
    // 用具名静态数组而非 `QStringList() << ...` 临时量：后者在 range-for 中
    // 会触发 GCC -Wmaybe-uninitialized 误报，且每次调用都重新构造临时对象。
    static const QStringList kFallbackFormats = {QStringLiteral("png"),  QStringLiteral("jpg"),
                                                 QStringLiteral("jpeg"), QStringLiteral("bmp"),
                                                 QStringLiteral("gif"),  QStringLiteral("webp"),
                                                 QStringLiteral("tiff"), QStringLiteral("tif")};
    for (const QString& f : kFallbackFormats) {
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

QString FileUtils::generateUniqueOutputPath(const QString& dir, const QString& baseName,
                                            const QString& suffix)
{
    const QString& cleanBase = baseName;
    QString ext = suffix;
    if (ext.startsWith(QStringLiteral(".")))
        ext = ext.mid(1);

    QString outputDir = dir;
    if (outputDir.isEmpty())
        outputDir = QDir::currentPath();

    QDir d(outputDir);
    // mkpath 的返回值此前被忽略：目录创建失败时仍会返回一个路径，直到真正写入
    // 才失败，且没有任何诊断信息。这里直接返回空串，由调用方报错。
    if (!d.exists() && !QDir().mkpath(outputDir))
        return QString();

    // 注意：这里的「存在性检查 + 返回」不是原子的（TOCTOU）。同一进程内串行调用
    // 没有问题；若将来允许两个批处理任务并发运行，仍可能选中同名文件互相覆盖。
    // 届时需要改为用 QIODevice::NewOnly 原子占位。
    QString candidate = d.absoluteFilePath(cleanBase + QStringLiteral(".") + ext);
    if (!QFile::exists(candidate))
        return candidate;

    for (int i = 1; i < 10000; ++i) {
        candidate = d.absoluteFilePath(QStringLiteral("%1_%2.%3").arg(cleanBase).arg(i).arg(ext));
        if (!QFile::exists(candidate))
            return candidate;
    }
    // 10000 个候选名全部被占用：原实现会把最后一个「已存在」的路径返回给调用方，
    // 造成静默覆盖用户文件。改为返回空串，由调用方判定为失败。
    return QString();
}

QString FileUtils::reserveUniqueOutputPath(const QString& dir, const QString& baseName,
                                           const QString& suffix)
{
    const QString& cleanBase = baseName;
    QString ext = suffix;
    if (ext.startsWith(QStringLiteral(".")))
        ext = ext.mid(1);

    QString outputDir = dir;
    if (outputDir.isEmpty())
        outputDir = QDir::currentPath();

    QDir d(outputDir);
    if (!d.exists() && !QDir().mkpath(outputDir))
        return QString();

    // 原子占位：以 NewOnly 打开，若文件已存在则打开失败。
    // 「检查存在 + 占位」合为一步系统调用，两个并发批处理任务不可能拿到同一文件名，
    // 消除了 generateUniqueOutputPath 的 TOCTOU 竞态。
    auto tryReserve = [&d](const QString& fileName) -> QString {
        const QString path = d.absoluteFilePath(fileName);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return QString();
        file.close();
        return path;
    };

    QString path = tryReserve(cleanBase + QStringLiteral(".") + ext);
    if (!path.isEmpty())
        return path;

    for (int i = 1; i < 10000; ++i) {
        path = tryReserve(QStringLiteral("%1_%2.%3").arg(cleanBase).arg(i).arg(ext));
        if (!path.isEmpty())
            return path;
    }
    // 10000 个候选名全部被占用：返回空串，由调用方判定为失败，
    // 绝不返回「已存在」的路径造成静默覆盖。
    return QString();
}

void FileUtils::discardReservedPath(const QString& path)
{
    // 仅清理本模块预留的空占位文件。写入成功后文件非空，不会被误删；
    // 写入失败时必须删掉占位，否则会留下 0 字节的「假成功」产物。
    if (path.isEmpty())
        return;
    QFileInfo info(path);
    if (info.exists() && info.size() == 0)
        QFile::remove(path);
}

bool FileUtils::saveImage(const QImage& image, const QString& path, const QString& format,
                          int quality)
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
