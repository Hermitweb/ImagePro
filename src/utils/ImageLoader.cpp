#include "ImageLoader.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>
#include <QPixmap>
#include <QtMath>

#ifdef USE_LIBVIPS
#ifdef signals
#undef signals
#endif
#include <vips/vips.h>
#include <cstring>
#endif

namespace yingtu {

#ifdef USE_LIBVIPS

class VipsInitHelper
{
public:
    static VipsInitHelper& instance()
    {
        static VipsInitHelper inst;
        return inst;
    }

    bool ok() const { return m_ok; }

private:
    VipsInitHelper()
    {
        if (VIPS_INIT("ImagePro"))
            m_ok = false;
        else
            m_ok = true;
    }
    ~VipsInitHelper()
    {
        // vips_shutdown() 会在静态析构阶段触发 GLib-GObject-CRITICAL 警告。
        // libvips 在进程退出时会自动清理，这里显式调用反而可能与 Qt 资源释放顺序冲突。
        // if (m_ok)
        //     vips_shutdown();
    }
    bool m_ok = false;
};

static void ensureVipsInitialized()
{
    (void)VipsInitHelper::instance();
}

QImage ImageLoader::vipsImageToQImage(VipsImage* image)
{
    const int width = vips_image_get_width(image);
    const int height = vips_image_get_height(image);
    if (width <= 0 || height <= 0)
        return QImage();

    // 转为 8-bit sRGB，带 alpha
    VipsImage* rgba = nullptr;
    if (vips_colourspace(image, &rgba, VIPS_INTERPRETATION_sRGB, nullptr))
        return QImage();

    const int bands = vips_image_get_bands(rgba);
    if (bands < 1 || bands > 4) {
        g_object_unref(rgba);
        return QImage();
    }

    // 写出为 chunky 8-bit 内存缓冲
    size_t size = 0;
    void* mem = vips_image_write_to_memory(rgba, &size);
    g_object_unref(rgba);
    if (!mem)
        return QImage();

    QImage result;
    if (bands == 4) {
        // libvips 输出是 RGBA，需要转成 Qt 的 ARGB
        QImage tmp(static_cast<uchar*>(mem), width, height, width * 4, QImage::Format_RGB32);
        result = tmp.convertToFormat(QImage::Format_ARGB32);
        // Qt 的 Format_ARGB32 是 0xAARRGGBB，libvips RGBA 是 R G B A 字节序
        // 在大端/小端上需要手动调整；x86 是 little-endian，简单交换即可
        for (int y = 0; y < height; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < width; ++x) {
                QRgb p = line[x];
                line[x] = qRgba(qBlue(p), qGreen(p), qRed(p), qAlpha(p));
            }
        }
    } else if (bands == 3) {
        QImage tmp(static_cast<uchar*>(mem), width, height, width * 3, QImage::Format_RGB888);
        result = tmp.copy();
    } else if (bands == 1) {
        QImage tmp(static_cast<uchar*>(mem), width, height, width, QImage::Format_Grayscale8);
        result = tmp.copy();
    }

    g_free(mem);
    return result;
}

VipsImage* ImageLoader::qImageToVipsImage(const QImage& image)
{
    if (image.isNull())
        return nullptr;

    ensureVipsInitialized();

    const int width = image.width();
    const int height = image.height();

    QImage src = image;
    VipsInterpretation interpretation = VIPS_INTERPRETATION_sRGB;
    int bands = 0;
    QByteArray buffer;

    switch (src.format()) {
    case QImage::Format_RGB888:
        bands = 3;
        buffer = QByteArray(reinterpret_cast<const char*>(src.constBits()), src.sizeInBytes());
        break;
    case QImage::Format_Grayscale8:
        bands = 1;
        interpretation = VIPS_INTERPRETATION_B_W;
        buffer = QByteArray(reinterpret_cast<const char*>(src.constBits()), src.sizeInBytes());
        break;
    case QImage::Format_ARGB32:
    case QImage::Format_RGB32: {
        bands = 4;
        // Qt 小端下 ARGB32 内存是 BGRA，转成 libvips 需要的 RGBA
        src = src.convertToFormat(QImage::Format_ARGB32);
        buffer.resize(width * height * 4);
        const uchar* bits = src.constBits();
        for (int y = 0; y < height; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(bits + y * src.bytesPerLine());
            uchar* out = reinterpret_cast<uchar*>(buffer.data()) + y * width * 4;
            for (int x = 0; x < width; ++x) {
                QRgb p = line[x];
                out[x * 4 + 0] = static_cast<uchar>(qRed(p));
                out[x * 4 + 1] = static_cast<uchar>(qGreen(p));
                out[x * 4 + 2] = static_cast<uchar>(qBlue(p));
                out[x * 4 + 3] = static_cast<uchar>(qAlpha(p));
            }
        }
        break;
    }
    default:
        // 其他格式统一转成 RGBA 处理
        src = src.convertToFormat(QImage::Format_ARGB32);
        bands = 4;
        buffer.resize(width * height * 4);
        for (int y = 0; y < height; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(src.constBits() + y * src.bytesPerLine());
            uchar* out = reinterpret_cast<uchar*>(buffer.data()) + y * width * 4;
            for (int x = 0; x < width; ++x) {
                QRgb p = line[x];
                out[x * 4 + 0] = static_cast<uchar>(qRed(p));
                out[x * 4 + 1] = static_cast<uchar>(qGreen(p));
                out[x * 4 + 2] = static_cast<uchar>(qBlue(p));
                out[x * 4 + 3] = static_cast<uchar>(qAlpha(p));
            }
        }
        break;
    }

    // 使用 _copy 版本让 libvips 立即复制并持有缓冲，避免局部 QByteArray 释放后
    // 出现 use-after-free（vips_copy 对内存源图像可能是惰性引用，不保证深拷贝）。
    VipsImage* mem = vips_image_new_from_memory_copy(buffer.constData(), buffer.size(),
                                                     width, height, bands,
                                                     VIPS_FORMAT_UCHAR);
    if (!mem)
        return nullptr;

    // 设置正确的色彩空间解释
    VipsImage* copied = nullptr;
    if (vips_copy(mem, &copied,
                  "interpretation", interpretation,
                  nullptr)) {
        g_object_unref(mem);
        vips_error_clear();
        return nullptr;
    }
    g_object_unref(mem);
    return copied;
}

ImageInfo ImageLoader::loadInfo(const QString& filePath)
{
    ensureVipsInitialized();

    ImageInfo info;
    info.filePath = filePath;

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        info.errorString = QObject::tr("File not found");
        return info;
    }
    info.fileSize = fi.size();

    VipsImage* image = vips_image_new_from_file(filePath.toUtf8().constData(),
        "access", VIPS_ACCESS_SEQUENTIAL,
        nullptr);
    if (!image) {
        info.errorString = QString::fromUtf8(vips_error_buffer());
        vips_error_clear();
        return info;
    }

    info.width = vips_image_get_width(image);
    info.height = vips_image_get_height(image);
    info.format = QFileInfo(filePath).suffix().toLower();
    info.valid = true;
    g_object_unref(image);
    return info;
}

QImage ImageLoader::loadImage(const QString& filePath)
{
    ensureVipsInitialized();

    VipsImage* image = vips_image_new_from_file(filePath.toUtf8().constData(),
        "access", VIPS_ACCESS_SEQUENTIAL,
        nullptr);
    if (!image)
        return QImage();

    QImage result = vipsImageToQImage(image);
    g_object_unref(image);
    return result;
}

QImage ImageLoader::loadPreview(const QString& filePath, const QSize& maxSize)
{
    ensureVipsInitialized();

    if (maxSize.isEmpty())
        return loadImage(filePath);

    // vips_thumbnail 结合了加载与缩放，对大图性能更好
    VipsImage* thumb = nullptr;
    if (vips_thumbnail(filePath.toUtf8().constData(), &thumb, maxSize.width(),
            "height", maxSize.height(),
            "size", VIPS_SIZE_DOWN,
            nullptr)) {
        vips_error_clear();
        return QImage();
    }

    QImage img = vipsImageToQImage(thumb);
    g_object_unref(thumb);
    return img;
}

QPixmap ImageLoader::loadThumbnail(const QString& filePath, int size)
{
    ensureVipsInitialized();

    if (size <= 0)
        return QPixmap();

    VipsImage* thumb = nullptr;
    if (vips_thumbnail(filePath.toUtf8().constData(), &thumb, size,
            "height", size,
            "size", VIPS_SIZE_DOWN,
            nullptr)) {
        vips_error_clear();
        return QPixmap();
    }

    QImage img = vipsImageToQImage(thumb);
    g_object_unref(thumb);
    return QPixmap::fromImage(img);
}

static bool saveVipsImage(VipsImage* image, const QString& outputPath, const QString& format, int quality)
{
    QString fmt = format.toLower();
    if (fmt.isEmpty())
        fmt = QFileInfo(outputPath).suffix().toLower();

    QByteArray path = outputPath.toUtf8();
    int result = 0;

    if (fmt == QStringLiteral("jpg") || fmt == QStringLiteral("jpeg")) {
        int q = quality >= 0 ? quality : 90;
        result = vips_jpegsave(image, path.constData(), "Q", q, NULL);
    } else if (fmt == QStringLiteral("webp")) {
        int q = quality >= 0 ? quality : 90;
        result = vips_webpsave(image, path.constData(), "Q", q, NULL);
    } else if (fmt == QStringLiteral("png")) {
        result = vips_pngsave(image, path.constData(), NULL);
    } else if (fmt == QStringLiteral("tif") || fmt == QStringLiteral("tiff")) {
        result = vips_tiffsave(image, path.constData(), NULL);
    } else {
        result = vips_image_write_to_file(image, path.constData(), NULL);
    }

    if (result)
        vips_error_clear();
    return result == 0;
}

bool ImageLoader::saveThumbnail(const QString& inputPath, const QString& outputPath,
                                const QSize& targetSize, const QString& format, int quality)
{
    ensureVipsInitialized();

    if (targetSize.isEmpty())
        return convertFile(inputPath, outputPath, format, quality);

    VipsImage* thumb = nullptr;
    if (vips_thumbnail(inputPath.toUtf8().constData(), &thumb, targetSize.width(),
            "height", targetSize.height(),
            "size", VIPS_SIZE_DOWN,
            nullptr)) {
        vips_error_clear();
        return false;
    }

    bool ok = saveVipsImage(thumb, outputPath, format, quality);
    g_object_unref(thumb);
    return ok;
}

bool ImageLoader::convertFile(const QString& inputPath, const QString& outputPath,
                              const QString& format, int quality)
{
    ensureVipsInitialized();

    VipsImage* image = vips_image_new_from_file(inputPath.toUtf8().constData(),
        "access", VIPS_ACCESS_SEQUENTIAL,
        nullptr);
    if (!image)
        return false;

    bool ok = saveVipsImage(image, outputPath, format, quality);
    g_object_unref(image);
    return ok;
}

bool ImageLoader::resizeFile(const QString& inputPath, const QString& outputPath,
                             const QSize& targetSize, bool keepAspect,
                             int kernel, const QString& format, int quality)
{
    ensureVipsInitialized();

    if (targetSize.isEmpty())
        return convertFile(inputPath, outputPath, format, quality);

    VipsImage* image = vips_image_new_from_file(inputPath.toUtf8().constData(),
        "access", VIPS_ACCESS_SEQUENTIAL,
        nullptr);
    if (!image)
        return false;

    const int w = vips_image_get_width(image);
    const int h = vips_image_get_height(image);
    if (w <= 0 || h <= 0) {
        g_object_unref(image);
        return false;
    }

    int tw = targetSize.width();
    int th = targetSize.height();
    if (keepAspect) {
        QSize s = QSize(w, h).scaled(tw, th, Qt::KeepAspectRatio);
        tw = s.width();
        th = s.height();
    }

    double hscale = static_cast<double>(tw) / w;
    double vscale = static_cast<double>(th) / h;

    VipsImage* resized = nullptr;
    VipsKernel k = VIPS_KERNEL_LINEAR;
    switch (kernel) {
    case 0: k = VIPS_KERNEL_NEAREST; break;
    case 2: k = VIPS_KERNEL_CUBIC; break;
    case 3: k = VIPS_KERNEL_LANCZOS3; break;
    default: k = VIPS_KERNEL_LINEAR; break;
    }

    if (vips_resize(image, &resized, hscale, "vscale", vscale, "kernel", k, nullptr)) {
        g_object_unref(image);
        vips_error_clear();
        return false;
    }
    g_object_unref(image);

    bool ok = saveVipsImage(resized, outputPath, format, quality);
    g_object_unref(resized);
    return ok;
}

bool ImageLoader::convertToSRgbFile(const QString& inputPath, const QString& outputPath,
                                    const QString& format, int quality)
{
    ensureVipsInitialized();

    VipsImage* image = vips_image_new_from_file(inputPath.toUtf8().constData(),
        "access", VIPS_ACCESS_SEQUENTIAL,
        nullptr);
    if (!image)
        return false;

    VipsImage* srgb = nullptr;
    if (vips_colourspace(image, &srgb, VIPS_INTERPRETATION_sRGB, nullptr)) {
        g_object_unref(image);
        vips_error_clear();
        return false;
    }
    g_object_unref(image);

    bool ok = saveVipsImage(srgb, outputPath, format, quality);
    g_object_unref(srgb);
    return ok;
}

bool ImageLoader::compressToTargetSize(const QString& inputPath, const QString& outputPath,
                                       const QSize& targetSize, qint64 targetBytes,
                                       const QString& format, int quality)
{
    ensureVipsInitialized();

    VipsImage* image = nullptr;
    if (!targetSize.isEmpty()) {
        // 利用 thumbnail 在加载阶段完成缩放，对大图更省内存
        if (vips_thumbnail(inputPath.toUtf8().constData(), &image, targetSize.width(),
                "height", targetSize.height(),
                "size", VIPS_SIZE_DOWN,
                nullptr)) {
            vips_error_clear();
            return false;
        }
    } else {
        image = vips_image_new_from_file(inputPath.toUtf8().constData(),
            "access", VIPS_ACCESS_SEQUENTIAL,
            nullptr);
        if (!image)
            return false;
    }

    QString fmt = format.toLower();
    if (fmt.isEmpty())
        fmt = QFileInfo(outputPath).suffix().toLower();

    const bool isJpeg = (fmt == QStringLiteral("jpg") || fmt == QStringLiteral("jpeg"));
    const bool isWebp = (fmt == QStringLiteral("webp"));
    if (!isJpeg && !isWebp) {
        g_object_unref(image);
        return false;
    }

    // targetBytes 为 0 时直接按 quality 保存
    if (targetBytes <= 0) {
        bool ok = saveVipsImage(image, outputPath, fmt, quality);
        g_object_unref(image);
        return ok;
    }

    int q = quality >= 0 ? quality : 90;
    int low = 10, high = 95;
    int bestQ = q;
    qint64 bestDiff = -1;

    // 二分查找逼近目标文件大小
    for (int attempt = 0; attempt < 12; ++attempt) {
        void* buf = nullptr;
        size_t len = 0;
        int err = 0;
        if (isJpeg)
            err = vips_jpegsave_buffer(image, &buf, &len, "Q", q, nullptr);
        else
            err = vips_webpsave_buffer(image, &buf, &len, "Q", q, nullptr);

        if (err) {
            vips_error_clear();
            g_object_unref(image);
            return false;
        }

        qint64 diff = qAbs(static_cast<qint64>(len) - targetBytes);
        if (bestDiff < 0 || diff < bestDiff) {
            bestDiff = diff;
            bestQ = q;
        }

        if (diff < targetBytes * 0.1 || q <= low || q >= high) {
            g_free(buf);
            break;
        }

        if (static_cast<qint64>(len) > targetBytes)
            high = q - 1;
        else
            low = q + 1;

        q = (low + high) / 2;
        g_free(buf);
    }

    bool ok = saveVipsImage(image, outputPath, fmt, bestQ);
    g_object_unref(image);
    return ok;
}

bool ImageLoader::saveImage(const QImage& image, const QString& outputPath,
                            const QString& format, int quality)
{
    ensureVipsInitialized();

    VipsImage* vips = qImageToVipsImage(image);
    if (!vips)
        return image.save(outputPath, format.toUpper().toUtf8(), quality);

    bool ok = saveVipsImage(vips, outputPath, format, quality);
    g_object_unref(vips);
    if (ok)
        return true;

    // libvips 保存失败时回退到 QImage
    return image.save(outputPath, format.toUpper().toUtf8(), quality);
}

#else // USE_LIBVIPS

ImageInfo ImageLoader::loadInfo(const QString& filePath)
{
    ImageInfo info;
    info.filePath = filePath;

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        info.errorString = QObject::tr("File not found");
        return info;
    }
    info.fileSize = fi.size();

    QImageReader reader(filePath);
    if (!reader.canRead()) {
        info.errorString = reader.errorString();
        return info;
    }
    info.format = reader.format();
    info.width = reader.size().width();
    info.height = reader.size().height();
    info.valid = true;
    return info;
}

QImage ImageLoader::loadImage(const QString& filePath)
{
    QImageReader reader(filePath);
    if (!reader.canRead())
        return QImage();
    return reader.read();
}

QImage ImageLoader::loadPreview(const QString& filePath, const QSize& maxSize)
{
    if (maxSize.isEmpty())
        return loadImage(filePath);

    QImageReader reader(filePath);
    if (!reader.canRead())
        return QImage();

    QSize originalSize = reader.size();
    if (!originalSize.isValid())
        return loadImage(filePath);

    reader.setScaledSize(originalSize.scaled(maxSize, Qt::KeepAspectRatio));
    return reader.read();
}

QPixmap ImageLoader::loadThumbnail(const QString& filePath, int size)
{
    QImageReader reader(filePath);
    if (!reader.canRead())
        return QPixmap();

    QSize targetSize(size, size);
    reader.setScaledSize(reader.size().scaled(targetSize, Qt::KeepAspectRatio));
    QImage img = reader.read();
    return QPixmap::fromImage(img);
}

bool ImageLoader::saveThumbnail(const QString& inputPath, const QString& outputPath,
                                const QSize& targetSize, const QString& format, int quality)
{
    QImage img = loadImage(inputPath);
    if (img.isNull())
        return false;

    if (!targetSize.isEmpty())
        img = img.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QString fmt = format;
    if (fmt.isEmpty())
        fmt = QFileInfo(outputPath).suffix();

    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0 ||
        fmt.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0 ||
        fmt.compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0) {
        int q = quality >= 0 ? quality : 90;
        return img.save(outputPath, fmt.toUpper().toUtf8(), q);
    }
    return img.save(outputPath, fmt.toUpper().toUtf8());
}

bool ImageLoader::convertFile(const QString& inputPath, const QString& outputPath,
                              const QString& format, int quality)
{
    return saveThumbnail(inputPath, outputPath, QSize(), format, quality);
}

bool ImageLoader::resizeFile(const QString& inputPath, const QString& outputPath,
                             const QSize& targetSize, bool keepAspect,
                             int /*kernel*/, const QString& format, int quality)
{
    QImage img = loadImage(inputPath);
    if (img.isNull())
        return false;

    Qt::AspectRatioMode aspect = keepAspect ? Qt::KeepAspectRatio : Qt::IgnoreAspectRatio;
    QImage out = img.scaled(targetSize, aspect, Qt::SmoothTransformation);

    QString fmt = format;
    if (fmt.isEmpty())
        fmt = QFileInfo(outputPath).suffix();

    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0 ||
        fmt.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0 ||
        fmt.compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0) {
        int q = quality >= 0 ? quality : 90;
        return out.save(outputPath, fmt.toUpper().toUtf8(), q);
    }
    return out.save(outputPath, fmt.toUpper().toUtf8());
}

bool ImageLoader::convertToSRgbFile(const QString& inputPath, const QString& outputPath,
                                    const QString& format, int quality)
{
    QImage img = loadImage(inputPath);
    if (img.isNull())
        return false;

    if (img.colorSpace().isValid()) {
        img = img.convertToFormat(QImage::Format_RGB32);
        img.setColorSpace(QColorSpace::SRgb);
    }

    QString fmt = format;
    if (fmt.isEmpty())
        fmt = QFileInfo(outputPath).suffix();

    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0 ||
        fmt.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0 ||
        fmt.compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0) {
        int q = quality >= 0 ? quality : 90;
        return img.save(outputPath, fmt.toUpper().toUtf8(), q);
    }
    return img.save(outputPath, fmt.toUpper().toUtf8());
}

bool ImageLoader::compressToTargetSize(const QString& inputPath, const QString& outputPath,
                                       const QSize& targetSize, qint64 targetBytes,
                                       const QString& format, int quality)
{
    QImage img = loadImage(inputPath);
    if (img.isNull())
        return false;

    if (!targetSize.isEmpty())
        img = img.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QString fmt = format;
    if (fmt.isEmpty())
        fmt = QFileInfo(outputPath).suffix();

    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    int q = quality >= 0 ? quality : 90;
    if (targetBytes > 0) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            QByteArray data;
            QBuffer buffer(&data);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, fmt.toUpper().toUtf8(), q);
            buffer.close();
            if (qAbs(static_cast<qint64>(data.size()) - targetBytes) < targetBytes * 0.1 || q <= 10 || q >= 95)
                break;
            if (data.size() > targetBytes)
                q = qMax(10, q - 10);
            else
                q = qMin(95, q + 5);
        }
    }

    return img.save(outputPath, fmt.toUpper().toUtf8(), q);
}

bool ImageLoader::saveImage(const QImage& image, const QString& outputPath,
                            const QString& format, int quality)
{
    QString fmt = format;
    if (fmt.isEmpty())
        fmt = QFileInfo(outputPath).suffix();

    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    return image.save(outputPath, fmt.toUpper().toUtf8(), quality);
}

#endif // USE_LIBVIPS

} // namespace yingtu
