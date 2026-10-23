#include "ImageLoader.h"
#include "ErrorHandler.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>
#include <QPixmap>
#include <QThread>
#include <QtMath>

#ifdef USE_LIBVIPS
#ifdef signals
#undef signals
#endif
#include <vips/vips.h>
#include <cstring>
#endif

namespace yingtu {

namespace {

/**
 * 把读取失败细分为「格式不认识」与「文件损坏」。
 *
 * 二者对用户是完全不同的处置：前者应提示换个格式，后者应提示重新导出。
 * libvips 与 QImageReader 的错误文本不同，但都含有可识别的关键词。
 */
ErrorCode classifyReadError(const QString& errorText)
{
    const QString e = errorText.toLower();
    if (e.contains(QLatin1String("no known loader")) ||
        e.contains(QLatin1String("not a known file format")) ||
        e.contains(QLatin1String("unsupported format")) ||
        e.contains(QLatin1String("unknown format")) ||
        e.contains(QLatin1String("no decode delegate"))) {
        return ErrorCode::FORMAT_UNSUPPORTED;
    }
    return ErrorCode::FILE_CORRUPTED;
}

/** 读取失败时统一上报：文件不存在 → FILE_NOT_FOUND，否则按错误文本细分。 */
void reportReadFailure(const QString& filePath, const QString& errorText, bool report = true)
{
    if (!report)
        return;
    if (!QFileInfo::exists(filePath)) {
        ErrorHandler::instance().report(ErrorCode::FILE_NOT_FOUND, filePath);
        return;
    }
    ErrorHandler::instance().report(classifyReadError(errorText),
                                    filePath + QStringLiteral(": ") + errorText);
}

} // namespace

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
        if (VIPS_INIT("ImagePro")) {
            m_ok = false;
        } else {
            m_ok = true;
            // 对交互式图像处理应用而言，操作缓存会随每次预览/处理无界增长，
            // 是虚拟内存暴涨的主要根因之一。关闭 memory / file 缓存，限制 operation
            // 缓存为少量条目，并限制 libvips 内部并发线程数，避免与 QtConcurrent 叠加后
            // 占用过多地址空间或产生线程竞争。
            vips_cache_set_max(32);
            vips_cache_set_max_mem(0);
            vips_cache_set_max_files(0);
            vips_concurrency_set(qMax(1, QThread::idealThreadCount() / 2));
        }
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

bool ImageLoader::initialize()
{
    ensureVipsInitialized();
    return VipsInitHelper::instance().ok();
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
        // libvips 输出是 RGBA，需要转成 Qt 的 ARGB。
        // 注意必须用 Format_ARGB32：Format_RGB32 会把 alpha 字节强制成 0xff，
        // 导致透明图加载后全部变成不透明。
        // 也不能用 convertToFormat(ARGB32)——同格式转换返回的是共享 mem 的浅拷贝，
        // g_free(mem) 之后会悬空；必须 copy() 深拷贝。
        QImage tmp(static_cast<uchar*>(mem), width, height, width * 4, QImage::Format_ARGB32);
        result = tmp.copy();
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
    } else if (bands == 2) {
        // 灰度 + alpha（如部分 PNG）。libvips 的 sRGB 色彩空间下一般为 GA，
        // 手动展开成 ARGB32，避免整张图被判为加载失败。
        result = QImage(width, height, QImage::Format_ARGB32);
        const uchar* p = static_cast<const uchar*>(mem);
        for (int y = 0; y < height; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            const uchar* src = p + static_cast<size_t>(y) * static_cast<size_t>(width) * 2;
            for (int x = 0; x < width; ++x) {
                const uchar g = src[static_cast<size_t>(x) * 2];
                const uchar a = src[static_cast<size_t>(x) * 2 + 1];
                line[x] = qRgba(g, g, g, a);
            }
        }
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
        // Qt 会把 RGB888 每行按 4 字节对齐填充，而 libvips 按紧凑行距
        // （width*bands）读取内存，直接传 constBits() 会让输出整体斜切。
        // 这里按 bytesPerLine 逐行重排成紧凑缓冲。
        buffer.resize(width * height * 3);
        for (int y = 0; y < height; ++y) {
            const uchar* line = src.constBits() + static_cast<size_t>(y) * src.bytesPerLine();
            std::memcpy(buffer.data() + static_cast<size_t>(y) * width * 3, line,
                        static_cast<size_t>(width) * 3);
        }
        break;
    case QImage::Format_Grayscale8:
        bands = 1;
        interpretation = VIPS_INTERPRETATION_B_W;
        // 同 RGB888：Grayscale8 每行同样按 4 字节对齐填充，必须逐行重排。
        buffer.resize(width * height);
        for (int y = 0; y < height; ++y) {
            const uchar* line = src.constBits() + static_cast<size_t>(y) * src.bytesPerLine();
            std::memcpy(buffer.data() + static_cast<size_t>(y) * width, line,
                        static_cast<size_t>(width));
        }
        break;
    case QImage::Format_ARGB32:
    case QImage::Format_RGB32: {
        bands = 4;
        // Qt 小端下 ARGB32 内存是 BGRA，转成 libvips 需要的 RGBA
        src = src.convertToFormat(QImage::Format_ARGB32);
        // 用 qsizetype 做乘法：大图的 width*height*4 会超出 int 范围。
        buffer.resize(static_cast<qsizetype>(width) * height * 4);
        const uchar* bits = src.constBits();
        for (int y = 0; y < height; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(
                bits + static_cast<size_t>(y) * static_cast<size_t>(src.bytesPerLine()));
            uchar* out = reinterpret_cast<uchar*>(buffer.data()) +
                         static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
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
        buffer.resize(static_cast<qsizetype>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(
                src.constBits() + static_cast<size_t>(y) * static_cast<size_t>(src.bytesPerLine()));
            uchar* out = reinterpret_cast<uchar*>(buffer.data()) +
                         static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
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
    VipsImage* mem = vips_image_new_from_memory_copy(buffer.constData(), buffer.size(), width,
                                                     height, bands, VIPS_FORMAT_UCHAR);
    if (!mem)
        return nullptr;

    // 设置正确的色彩空间解释
    VipsImage* copied = nullptr;
    if (vips_copy(mem, &copied, "interpretation", interpretation, nullptr)) {
        g_object_unref(mem);
        vips_error_clear();
        return nullptr;
    }
    g_object_unref(mem);
    return copied;
}

ImageInfo ImageLoader::loadInfo(const QString& filePath)
{
    return loadInfoImpl(filePath, true);
}

ImageInfo ImageLoader::loadInfoQuiet(const QString& filePath)
{
    return loadInfoImpl(filePath, false);
}

ImageInfo ImageLoader::loadInfoImpl(const QString& filePath, bool reportErrors)
{
    ensureVipsInitialized();

    ImageInfo info;
    info.filePath = filePath;

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        info.errorString = QObject::tr("File not found");
        if (reportErrors)
            ErrorHandler::instance().report(ErrorCode::FILE_NOT_FOUND, filePath);
        return info;
    }
    info.fileSize = fi.size();

    // SEQUENTIAL：单遍读取；全局 operation/memory/file cache 已禁用，避免 tile cache 累积。
    VipsImage* image = vips_image_new_from_file(filePath.toUtf8().constData(), "access",
                                                VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!image) {
        info.errorString = QString::fromUtf8(vips_error_buffer());
        vips_error_clear();
        reportReadFailure(filePath, info.errorString, reportErrors);
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

    // SEQUENTIAL：单遍读取；全局 operation/memory/file cache 已禁用。
    VipsImage* image = vips_image_new_from_file(filePath.toUtf8().constData(), "access",
                                                VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!image) {
        const QString err = QString::fromUtf8(vips_error_buffer());
        vips_error_clear();
        reportReadFailure(filePath, err);
        return QImage();
    }

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
    if (vips_thumbnail(filePath.toUtf8().constData(), &thumb, maxSize.width(), "height",
                       maxSize.height(), "size", VIPS_SIZE_DOWN, nullptr)) {
        vips_error_clear();
        return QImage();
    }

    QImage img = vipsImageToQImage(thumb);
    g_object_unref(thumb);
    return img;
}

static bool saveVipsImage(VipsImage* image, const QString& outputPath, const QString& format,
                          int quality)
{
    QString fmt = format.toLower();
    if (fmt.isEmpty())
        fmt = QFileInfo(outputPath).suffix().toLower();

    QByteArray path = outputPath.toUtf8();
    int result = 0;

    if (fmt == QStringLiteral("jpg") || fmt == QStringLiteral("jpeg")) {
        int q = quality >= 0 ? quality : 90;
        // JPEG 只接受 1 或 3 band。带 alpha 的输入（4 band RGBA，或灰度+alpha 的
        // 2 band）会让 jpegsave 直接报错，因此先以白色背景压平到 3 band。
        VipsImage* flat = nullptr;
        const int inBands = vips_image_get_bands(image);
        if (inBands == 4 || inBands == 2) {
            double bg[3] = {255.0, 255.0, 255.0};
            VipsArrayDouble* arr = vips_array_double_new(bg, 3);
            if (vips_flatten(image, &flat, "background", arr, nullptr))
                flat = nullptr;
            vips_area_unref(VIPS_AREA(arr));
        }
        VipsImage* target = flat ? flat : image;
        result = vips_jpegsave(target, path.constData(), "Q", q, NULL);
        if (flat)
            g_object_unref(flat);
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
    if (vips_thumbnail(inputPath.toUtf8().constData(), &thumb, targetSize.width(), "height",
                       targetSize.height(), "size", VIPS_SIZE_DOWN, nullptr)) {
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

    // SEQUENTIAL：单遍读取；全局 operation/memory/file cache 已禁用。
    VipsImage* image = vips_image_new_from_file(inputPath.toUtf8().constData(), "access",
                                                VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!image)
        return false;

    bool ok = saveVipsImage(image, outputPath, format, quality);
    g_object_unref(image);
    return ok;
}

bool ImageLoader::resizeFile(const QString& inputPath, const QString& outputPath,
                             const QSize& targetSize, bool keepAspect, int kernel,
                             const QString& format, int quality)
{
    ensureVipsInitialized();

    if (targetSize.isEmpty())
        return convertFile(inputPath, outputPath, format, quality);

    // SEQUENTIAL：单遍读取；全局 operation/memory/file cache 已禁用。
    VipsImage* image = vips_image_new_from_file(inputPath.toUtf8().constData(), "access",
                                                VIPS_ACCESS_SEQUENTIAL, nullptr);
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
    case 0:
        k = VIPS_KERNEL_NEAREST;
        break;
    case 2:
        k = VIPS_KERNEL_CUBIC;
        break;
    case 3:
        k = VIPS_KERNEL_LANCZOS3;
        break;
    default:
        k = VIPS_KERNEL_LINEAR;
        break;
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

    // SEQUENTIAL：单遍读取；全局 operation/memory/file cache 已禁用。
    VipsImage* image = vips_image_new_from_file(inputPath.toUtf8().constData(), "access",
                                                VIPS_ACCESS_SEQUENTIAL, nullptr);
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
        if (vips_thumbnail(inputPath.toUtf8().constData(), &image, targetSize.width(), "height",
                           targetSize.height(), "size", VIPS_SIZE_DOWN, nullptr)) {
            vips_error_clear();
            return false;
        }
    } else {
        // SEQUENTIAL：单遍读取；全局 operation/memory/file cache 已禁用。
        image = vips_image_new_from_file(inputPath.toUtf8().constData(), "access",
                                         VIPS_ACCESS_SEQUENTIAL, nullptr);
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

    // JPEG 只接受 1/3 band：带 alpha 的输入在 *_buffer 编码路径同样会失败，
    // 这里先压平一次，供整个二分过程复用。
    if (isJpeg) {
        const int inBands = vips_image_get_bands(image);
        if (inBands == 4 || inBands == 2) {
            double bg[3] = {255.0, 255.0, 255.0};
            VipsArrayDouble* arr = vips_array_double_new(bg, 3);
            VipsImage* flattened = nullptr;
            if (vips_flatten(image, &flattened, "background", arr, nullptr) == 0) {
                g_object_unref(image);
                image = flattened;
            }
            vips_area_unref(VIPS_AREA(arr));
        }
    }

    // 二分过程中会对同一图像反复编码；SEQUENTIAL 输入会因此反复重新解码源文件。
    // 先固化到内存，把最多 12 次潜在的文件读取降为 1 次。
    {
        VipsImage* cached = vips_image_copy_memory(image);
        if (cached) {
            g_object_unref(image);
            image = cached;
        } else {
            vips_error_clear();
        }
    }

    int q = quality >= 0 ? quality : 90;
    int low = 10, high = 95;
    int bestQ = q;
    qint64 bestDiff = -1;
    qint64 bestSize = 0;

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
            bestSize = static_cast<qint64>(len);
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
    if (!ok)
        return false;

    // 压不到目标体积是一个独立的失败原因（而非「写入失败」），
    // 只有在这里才知道最终体积，二分过程之外无从推断。
    if (bestSize > targetBytes) {
        ErrorHandler::instance().report(ErrorCode::OUTPUT_TOO_LARGE,
                                        QStringLiteral("%1: %2 bytes > target %3")
                                            .arg(outputPath)
                                            .arg(bestSize)
                                            .arg(targetBytes));
    }
    return true;
}

bool ImageLoader::saveImage(const QImage& image, const QString& outputPath, const QString& format,
                            int quality)
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

bool ImageLoader::initialize()
{
    // 未启用 libvips 时全部走 QImage/QImageReader 回退路径，恒为可用。
    return true;
}

ImageInfo ImageLoader::loadInfo(const QString& filePath)
{
    return loadInfoImpl(filePath, true);
}

ImageInfo ImageLoader::loadInfoQuiet(const QString& filePath)
{
    return loadInfoImpl(filePath, false);
}

ImageInfo ImageLoader::loadInfoImpl(const QString& filePath, bool reportErrors)
{
    ImageInfo info;
    info.filePath = filePath;

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        info.errorString = QObject::tr("File not found");
        if (reportErrors)
            ErrorHandler::instance().report(ErrorCode::FILE_NOT_FOUND, filePath);
        return info;
    }
    info.fileSize = fi.size();

    QImageReader reader(filePath);
    if (!reader.canRead()) {
        info.errorString = reader.errorString();
        reportReadFailure(filePath, info.errorString, reportErrors);
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
    if (!reader.canRead()) {
        reportReadFailure(filePath, reader.errorString());
        return QImage();
    }
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
                             const QSize& targetSize, bool keepAspect, int /*kernel*/,
                             const QString& format, int quality)
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
            if (qAbs(static_cast<qint64>(data.size()) - targetBytes) < targetBytes * 0.1 ||
                q <= 10 || q >= 95)
                break;
            if (data.size() > targetBytes)
                q = qMax(10, q - 10);
            else
                q = qMin(95, q + 5);
        }
    }

    return img.save(outputPath, fmt.toUpper().toUtf8(), q);
}

bool ImageLoader::saveImage(const QImage& image, const QString& outputPath, const QString& format,
                            int quality)
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
