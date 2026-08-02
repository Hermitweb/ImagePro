#include "StitchEngine.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QDir>
#include <QPainter>
#include <QtConcurrent/QtConcurrentRun>

#ifdef USE_LIBVIPS
#ifdef signals
#undef signals
#endif
#include <vips/vips.h>
#endif

namespace yingtu {

// 逐行扫描白边裁剪。使用 constScanLine 直接指针访问，比 pixel(x,y) 快 10×+。
static QImage cropWhiteEdges(const QImage& source, int threshold = 250)
{
    if (source.isNull())
        return source;

    // 统一到 32 位格式以便用 scanLine 做快速指针访问
    QImage img = (source.format() == QImage::Format_ARGB32 ||
                  source.format() == QImage::Format_RGB32)
                     ? source
                     : source.convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return source;

    const int w = img.width();
    const int h = img.height();

    auto rowHasContent = [&](int y) -> bool {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = row[x];
            if (qRed(c) < threshold || qGreen(c) < threshold || qBlue(c) < threshold)
                return true;
        }
        return false;
    };

    auto colHasContent = [&](int x, int top, int bottom) -> bool {
        for (int y = top; y <= bottom; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            const QRgb c = row[x];
            if (qRed(c) < threshold || qGreen(c) < threshold || qBlue(c) < threshold)
                return true;
        }
        return false;
    };

    int left = 0, top = 0, right = w - 1, bottom = h - 1;
    bool found = false;

    for (int y = 0; y < h; ++y) {
        if (rowHasContent(y)) {
            top = y;
            found = true;
            break;
        }
    }
    if (!found)
        return source;

    for (int y = h - 1; y >= 0; --y) {
        if (rowHasContent(y)) {
            bottom = y;
            break;
        }
    }

    for (int x = 0; x < w; ++x) {
        if (colHasContent(x, top, bottom)) {
            left = x;
            break;
        }
    }

    for (int x = w - 1; x >= 0; --x) {
        if (colHasContent(x, top, bottom)) {
            right = x;
            break;
        }
    }

    QRect rect(left, top, right - left + 1, bottom - top + 1);
    return source.copy(rect);
}

#ifdef USE_LIBVIPS

static VipsImage* loadVipsImage(const QString& path)
{
    // 拼接需要对多张图进行 resize/insert 组合，后续 vips_insert 评估管线时
    // 会多次读取输入；SEQUENTIAL 在此场景下会导致崩溃或读取失败，改用 RANDOM。
    return vips_image_new_from_file(path.toUtf8().constData(),
                                    "access", VIPS_ACCESS_RANDOM,
                                    nullptr);
}

static VipsImage* resizeVipsImage(VipsImage* in, int targetW, int targetH, bool keepAspect)
{
    if (!in)
        return nullptr;

    const int w = vips_image_get_width(in);
    const int h = vips_image_get_height(in);
    if (w <= 0 || h <= 0)
        return nullptr;

    double hscale = static_cast<double>(targetW) / w;
    double vscale = static_cast<double>(targetH) / h;
    if (keepAspect) {
        double scale = qMin(hscale, vscale);
        hscale = vscale = scale;
    }

    VipsImage* out = nullptr;
    if (vips_resize(in, &out, hscale, "vscale", vscale, nullptr))
        return nullptr;
    return out;
}

static VipsImage* createSolidCanvas(int width, int height, const QColor& color)
{
    unsigned char pixel[4] = {
        static_cast<unsigned char>(color.red()),
        static_cast<unsigned char>(color.green()),
        static_cast<unsigned char>(color.blue()),
        static_cast<unsigned char>(color.alpha())
    };

    VipsImage* tile = vips_image_new_from_memory_copy(pixel, 4, 1, 1, 4, VIPS_FORMAT_UCHAR);
    if (!tile)
        return nullptr;

    VipsImage* srgbTile = nullptr;
    if (vips_copy(tile, &srgbTile, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr)) {
        g_object_unref(tile);
        return nullptr;
    }
    g_object_unref(tile);

    VipsImage* canvas = nullptr;
    if (vips_replicate(srgbTile, &canvas, width, height, nullptr)) {
        g_object_unref(srgbTile);
        return nullptr;
    }
    g_object_unref(srgbTile);
    return canvas;
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

struct VipsImageInfo {
    VipsImage* image = nullptr;
    int originalWidth = 0;
    int originalHeight = 0;
    int displayWidth = 0;
    int displayHeight = 0;
};

static QSize limitLongEdge(const QSize& orig, int maxLongEdge)
{
    if (maxLongEdge <= 0)
        return orig;
    int longEdge = qMax(orig.width(), orig.height());
    if (longEdge <= maxLongEdge)
        return orig;
    return orig.scaled(maxLongEdge, maxLongEdge, Qt::KeepAspectRatio);
}

static QList<VipsImageInfo> loadAndPrepareImages(const QStringList& filePaths,
                                                  const StitchSettings& settings,
                                                  int maxLongEdge)
{
    QList<VipsImageInfo> infos;

    // 先统一获取原始尺寸，并应用 maxLongEdge 限制
    QList<QSize> originalSizes;
    QList<QSize> displaySizes;
    for (const QString& path : filePaths) {
        ImageInfo info = ImageLoader::loadInfo(path);
        if (info.valid) {
            QSize orig(info.width, info.height);
            originalSizes.append(orig);
            displaySizes.append(limitLongEdge(orig, maxLongEdge));
        } else {
            originalSizes.append(QSize());
            displaySizes.append(QSize());
        }
    }

    // 在已限制后的尺寸上计算统一目标尺寸
    int targetCellW = 0;
    int targetCellH = 0;
    if (settings.direction == StitchSettings::Grid) {
        for (const QSize& s : displaySizes) {
            if (!s.isEmpty()) {
                targetCellW = qMax(targetCellW, s.width());
                targetCellH = qMax(targetCellH, s.height());
            }
        }
    } else if (settings.direction == StitchSettings::Vertical && settings.uniformWidth) {
        for (const QSize& s : displaySizes) {
            if (!s.isEmpty())
                targetCellW = qMax(targetCellW, s.width());
        }
    } else if (settings.direction == StitchSettings::Horizontal && settings.uniformHeight) {
        for (const QSize& s : displaySizes) {
            if (!s.isEmpty())
                targetCellH = qMax(targetCellH, s.height());
        }
    }

    for (int i = 0; i < filePaths.size(); ++i) {
        VipsImage* img = loadVipsImage(filePaths.at(i));
        if (!img)
            continue;

        const QSize orig = originalSizes.at(i);
        const QSize limited = displaySizes.at(i);
        if (orig.isEmpty() || limited.isEmpty()) {
            g_object_unref(img);
            continue;
        }

        int displayW = limited.width();
        int displayH = limited.height();

        VipsImage* scaled = img;
        if (settings.direction == StitchSettings::Vertical && settings.uniformWidth && targetCellW > 0) {
            VipsImage* tmp = resizeVipsImage(img, targetCellW,
                qRound(orig.height() * (static_cast<double>(targetCellW) / orig.width())), true);
            if (tmp) {
                g_object_unref(scaled);
                scaled = tmp;
            }
        } else if (settings.direction == StitchSettings::Horizontal && settings.uniformHeight && targetCellH > 0) {
            VipsImage* tmp = resizeVipsImage(img,
                qRound(orig.width() * (static_cast<double>(targetCellH) / orig.height())), targetCellH, true);
            if (tmp) {
                g_object_unref(scaled);
                scaled = tmp;
            }
        } else if (settings.direction == StitchSettings::Grid && targetCellW > 0 && targetCellH > 0) {
            VipsImage* tmp = resizeVipsImage(img, targetCellW, targetCellH, true);
            if (tmp) {
                g_object_unref(scaled);
                scaled = tmp;
            }
        } else if (maxLongEdge > 0 && (orig.width() != displayW || orig.height() != displayH)) {
            VipsImage* tmp = resizeVipsImage(img, displayW, displayH, true);
            if (tmp) {
                g_object_unref(scaled);
                scaled = tmp;
            }
        }

        VipsImageInfo info;
        info.image = scaled;
        info.originalWidth = orig.width();
        info.originalHeight = orig.height();
        info.displayWidth = vips_image_get_width(scaled);
        info.displayHeight = vips_image_get_height(scaled);
        infos.append(info);

        if (scaled != img)
            g_object_unref(img);
    }

    return infos;
}

static VipsImage* stitchVipsImages(const QList<VipsImageInfo>& infos, const StitchSettings& settings,
                                    QVector<QRect>* outRects = nullptr)
{
    if (infos.isEmpty())
        return nullptr;

    int totalWidth = 0;
    int totalHeight = 0;

    if (settings.direction == StitchSettings::Horizontal) {
        int maxH = 0;
        for (const auto& info : infos) {
            totalWidth += info.displayWidth + settings.spacing;
            maxH = qMax(maxH, info.displayHeight);
        }
        totalWidth -= settings.spacing;
        totalHeight = maxH;
    } else if (settings.direction == StitchSettings::Vertical) {
        int maxW = 0;
        for (const auto& info : infos) {
            totalHeight += info.displayHeight + settings.spacing;
            maxW = qMax(maxW, info.displayWidth);
        }
        totalHeight -= settings.spacing;
        totalWidth = maxW;
    } else {
        int cellW = 0, cellH = 0;
        for (const auto& info : infos) {
            cellW = qMax(cellW, info.displayWidth);
            cellH = qMax(cellH, info.displayHeight);
        }
        totalWidth = settings.gridColumns * cellW + (settings.gridColumns - 1) * settings.spacing;
        totalHeight = settings.gridRows * cellH + (settings.gridRows - 1) * settings.spacing;
    }

    if (totalWidth <= 0 || totalHeight <= 0)
        return nullptr;

    QColor bgColor = settings.background == QStringLiteral("transparent") ? Qt::transparent : settings.bgColor;
    VipsImage* canvas = createSolidCanvas(totalWidth, totalHeight, bgColor);
    if (!canvas)
        return nullptr;

    VipsImage* current = canvas;

    // 预计算 Grid 单元格尺寸
    int cellW = 0, cellH = 0;
    if (settings.direction == StitchSettings::Grid) {
        for (const auto& info : infos) {
            cellW = qMax(cellW, info.displayWidth);
            cellH = qMax(cellH, info.displayHeight);
        }
    }

    int col = 0;
    for (int i = 0; i < infos.size(); ++i) {
        const auto& info = infos.at(i);

        // 统一计算每张图在画布中的 insert 坐标（左上角）。
        // 修复历史 Bug：Vertical 之前误用自增后的 y，导致图片整体下移、尾部越界丢失。
        int insertX = 0, insertY = 0;
        if (settings.direction == StitchSettings::Horizontal) {
            // 累加已处理图片宽度 + 间距
            for (int j = 0; j < i; ++j)
                insertX += infos.at(j).displayWidth + settings.spacing;
            insertY = (totalHeight - info.displayHeight) / 2; // 垂直居中
        } else if (settings.direction == StitchSettings::Vertical) {
            // 累加已处理图片高度 + 间距
            for (int j = 0; j < i; ++j)
                insertY += infos.at(j).displayHeight + settings.spacing;
            insertX = (totalWidth - info.displayWidth) / 2; // 水平居中（修复左对齐）
        } else { // Grid
            insertX = (col % settings.gridColumns) * (cellW + settings.spacing);
            insertY = (col / settings.gridColumns) * (cellH + settings.spacing);
            ++col;
        }

        if (outRects)
            outRects->append(QRect(insertX, insertY, info.displayWidth, info.displayHeight));

        VipsImage* inserted = nullptr;
        if (vips_insert(current, info.image, &inserted, insertX, insertY, nullptr)) {
            g_object_unref(current);
            return nullptr;
        }
        g_object_unref(current);
        current = inserted;
    }

    return current;
}

static void freeVipsImageInfos(QList<VipsImageInfo>& infos)
{
    for (auto& info : infos) {
        if (info.image)
            g_object_unref(info.image);
        info.image = nullptr;
    }
    infos.clear();
}

// 估算 Stitch 最终输出像素数，用于在分配巨大画布前拒绝危险操作。
// 仅读取图片头信息估算最终输出尺寸，不加载像素数据。
static QSize estimateOutputSize(const QStringList& filePaths, const StitchSettings& settings)
{
    QList<QSize> sizes;
    sizes.reserve(filePaths.size());
    for (const QString& path : filePaths) {
        ImageInfo info = ImageLoader::loadInfo(path);
        if (info.valid)
            sizes.append(QSize(info.width, info.height));
    }

    const int n = sizes.size();
    if (n == 0)
        return QSize();

    if (settings.direction == StitchSettings::Horizontal) {
        int maxH = 0;
        qint64 totalW = 0;
        for (const QSize& s : sizes) {
            maxH = qMax(maxH, s.height());
            totalW += s.width();
        }
        if (settings.uniformHeight && maxH > 0) {
            totalW = 0;
            for (const QSize& s : sizes)
                totalW += qRound(s.width() * double(maxH) / s.height());
        }
        totalW += qint64(n - 1) * settings.spacing;
        return QSize(int(totalW), maxH);
    }

    if (settings.direction == StitchSettings::Vertical) {
        int maxW = 0;
        qint64 totalH = 0;
        for (const QSize& s : sizes) {
            maxW = qMax(maxW, s.width());
            totalH += s.height();
        }
        if (settings.uniformWidth && maxW > 0) {
            totalH = 0;
            for (const QSize& s : sizes)
                totalH += qRound(s.height() * double(maxW) / s.width());
        }
        totalH += qint64(n - 1) * settings.spacing;
        return QSize(maxW, int(totalH));
    }

    // Grid
    int maxW = 0, maxH = 0;
    for (const QSize& s : sizes) {
        maxW = qMax(maxW, s.width());
        maxH = qMax(maxH, s.height());
    }
    qint64 totalW = qint64(settings.gridColumns) * maxW
                    + qint64(settings.gridColumns - 1) * settings.spacing;
    qint64 totalH = qint64(settings.gridRows) * maxH
                    + qint64(settings.gridRows - 1) * settings.spacing;
    return QSize(int(totalW), int(totalH));
}

// 估算 Stitch 最终输出像素数，用于在分配巨大画布前拒绝危险操作。
static qint64 estimateOutputPixels(const QStringList& filePaths, const StitchSettings& settings)
{
    const QSize sz = estimateOutputSize(filePaths, settings);
    return qint64(sz.width()) * qint64(sz.height());
}

static QImage vipsStitchPreview(const QStringList& filePaths, const StitchSettings& settings,
                                 int maxLongEdge, QVector<QRect>* outRects = nullptr)
{
    QList<VipsImageInfo> infos = loadAndPrepareImages(filePaths, settings, maxLongEdge);
    VipsImage* stitched = stitchVipsImages(infos, settings, outRects);
    freeVipsImageInfos(infos);

    if (!stitched)
        return QImage();

    QImage result = ImageLoader::vipsImageToQImage(stitched);
    g_object_unref(stitched);

    if (settings.autoCropEdges)
        result = cropWhiteEdges(result);
    return result;
}

static bool vipsStitchProcess(const QStringList& filePaths, const StitchSettings& settings, const QString& outputPath)
{
    QList<VipsImageInfo> infos = loadAndPrepareImages(filePaths, settings, 0);
    VipsImage* stitched = stitchVipsImages(infos, settings);
    freeVipsImageInfos(infos);

    if (!stitched)
        return false;

    bool ok = saveVipsImage(stitched, outputPath, settings.outputFormat, settings.quality);
    g_object_unref(stitched);
    return ok;
}

#endif // USE_LIBVIPS

StitchEngine::StitchEngine(QObject* parent)
    : QObject(parent)
{
}

QString StitchEngine::process(const QStringList& filePaths, bool* ok)
{
    if (ok) *ok = false;
    if (filePaths.isEmpty()) {
        emit error(tr("No images to stitch"));
        return QString();
    }

    QString outputPath = m_settings.explicitOutputPath;
    if (outputPath.isEmpty()) {
        QString dir = m_settings.outputDir;
        if (dir.isEmpty())
            dir = QFileInfo(filePaths.first()).absolutePath();

        QString suffix = QStringLiteral(".") + m_settings.outputFormat.toLower();
        outputPath = FileUtils::generateUniqueOutputPath(dir, m_settings.baseName, suffix);
    }

    // 输出尺寸安全阈值：在分配画布前拒绝会导致虚拟内存暴涨的危险操作。
    constexpr qint64 kMaxOutputPixels = 1'000'000'000LL; // 约 4GB ARGB
    const qint64 estimatedPixels = estimateOutputPixels(filePaths, m_settings);
    if (estimatedPixels > kMaxOutputPixels) {
        emit error(tr("Stitched image too large: estimated %1 pixels (limit %2). "
                      "Please reduce image count, resolution, or enable downscaling.")
                       .arg(estimatedPixels).arg(kMaxOutputPixels));
        return QString();
    }

#ifdef USE_LIBVIPS
    // removeWhiteEdges 需要对单张输入图做白边裁剪，仍走 QImage 路径；
    // autoCropEdges 只需对最终结果裁剪，先走 libvips 拼接再转 QImage 裁剪；
    // 正常情况直接走 libvips 写出文件，不经过 QImage，内存最优。
    if (!m_settings.removeWhiteEdges) {
        if (!m_settings.autoCropEdges) {
            if (vipsStitchProcess(filePaths, m_settings, outputPath)) {
                if (ok) *ok = true;
                emit finished(outputPath);
                return outputPath;
            }
        } else {
            QImage result = vipsStitchPreview(filePaths, m_settings, 0);
            if (!result.isNull()) {
                result = cropWhiteEdges(result);
                if (ImageLoader::saveImage(result, outputPath, m_settings.outputFormat, m_settings.quality)) {
                    if (ok) *ok = true;
                    emit finished(outputPath);
                    return outputPath;
                }
            }
        }
        // libvips 路径失败时回退到 QImage 路径
    }
#endif

    QImage result = preview(filePaths, m_settings);
    if (result.isNull()) {
        emit error(tr("Failed to generate stitched image"));
        return QString();
    }

    bool saved = ImageLoader::saveImage(result, outputPath, m_settings.outputFormat, m_settings.quality);

    if (!saved) {
        emit error(tr("Failed to save stitched image"));
        return QString();
    }

    if (ok) *ok = true;
    emit finished(outputPath);
    return outputPath;
}

QImage StitchEngine::preview(const QStringList& filePaths, const StitchSettings& settings,
                              int maxLongEdge, QVector<QRect>* inputRects)
{
    if (inputRects)
        inputRects->clear();

#ifdef USE_LIBVIPS
    if (!settings.removeWhiteEdges) {
        QImage img = vipsStitchPreview(filePaths, settings, maxLongEdge, inputRects);
        if (!img.isNull()) {
            // autoCropEdges 会裁剪最终结果，使 rects 坐标失效，清空以保证高亮准确。
            if (settings.autoCropEdges && inputRects)
                inputRects->clear();
            return img;
        }
    }
#endif

    QList<QImage> images;

    for (const QString& path : filePaths) {
        QImage img;
        if (maxLongEdge > 0) {
            ImageInfo info = ImageLoader::loadInfo(path);
            if (info.valid) {
                QSize orig(info.width, info.height);
                QSize limited = limitLongEdge(orig, maxLongEdge);
                if (limited != orig)
                    img = ImageLoader::loadPreview(path, limited);
            }
        }
        if (img.isNull())
            img = ImageLoader::loadImage(path);
        if (img.isNull())
            continue;
        if (settings.removeWhiteEdges)
            img = cropWhiteEdges(img);
        images.append(img);
    }

    if (images.isEmpty())
        return QImage();

    // 统一尺寸
    if (settings.uniformWidth && settings.direction == StitchSettings::Vertical) {
        int maxW = 0;
        for (const QImage& img : images)
            maxW = qMax(maxW, img.width());
        for (QImage& img : images) {
            if (img.width() != maxW)
                img = img.scaledToWidth(maxW, Qt::SmoothTransformation);
        }
    } else if (settings.uniformHeight && settings.direction == StitchSettings::Horizontal) {
        int maxH = 0;
        for (const QImage& img : images)
            maxH = qMax(maxH, img.height());
        for (QImage& img : images) {
            if (img.height() != maxH)
                img = img.scaledToHeight(maxH, Qt::SmoothTransformation);
        }
    } else if (settings.direction == StitchSettings::Grid) {
        int maxW = 0, maxH = 0;
        for (const QImage& img : images) {
            maxW = qMax(maxW, img.width());
            maxH = qMax(maxH, img.height());
        }
        for (QImage& img : images) {
            if (img.width() != maxW || img.height() != maxH)
                img = img.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    int totalWidth = 0;
    int totalHeight = 0;

    if (settings.direction == StitchSettings::Horizontal) {
        int maxH = 0;
        for (const QImage& img : images) {
            totalWidth += img.width() + settings.spacing;
            maxH = qMax(maxH, img.height());
        }
        totalWidth -= settings.spacing;
        totalHeight = maxH;
    } else if (settings.direction == StitchSettings::Vertical) {
        int maxW = 0;
        for (const QImage& img : images) {
            totalHeight += img.height() + settings.spacing;
            maxW = qMax(maxW, img.width());
        }
        totalHeight -= settings.spacing;
        totalWidth = maxW;
    } else {
        int cols = settings.gridColumns;
        int rows = settings.gridRows;
        int cellW = 0;
        int cellH = 0;
        for (const QImage& img : images) {
            cellW = qMax(cellW, img.width());
            cellH = qMax(cellH, img.height());
        }
        totalWidth = cols * cellW + (cols - 1) * settings.spacing;
        totalHeight = rows * cellH + (rows - 1) * settings.spacing;
    }

    QImage result(totalWidth, totalHeight, QImage::Format_ARGB32);
    QColor bg = settings.background == QStringLiteral("transparent") ? Qt::transparent : settings.bgColor;
    result.fill(bg);

    QPainter painter(&result);
    int col = 0;

    // 预计算 Grid 单元格尺寸
    auto cellSize = [&]() -> QSize {
        if (settings.direction != StitchSettings::Grid)
            return QSize();
        int cw = 0, ch = 0;
        for (const QImage& img : images) {
            cw = qMax(cw, img.width());
            ch = qMax(ch, img.height());
        }
        return QSize(cw, ch);
    }();

    for (int i = 0; i < images.size(); ++i) {
        const QImage& img = images[i];
        int drawX = 0, drawY = 0;
        if (settings.direction == StitchSettings::Horizontal) {
            // 累加已绘制图片宽度 + 间距
            for (int j = 0; j < i; ++j)
                drawX += images[j].width() + settings.spacing;
            drawY = (totalHeight - img.height()) / 2; // 垂直居中（与 VIPS 路径一致）
        } else if (settings.direction == StitchSettings::Vertical) {
            // 累加已绘制图片高度 + 间距
            for (int j = 0; j < i; ++j)
                drawY += images[j].height() + settings.spacing;
            drawX = (totalWidth - img.width()) / 2; // 水平居中（修复左对齐）
        } else {
            drawX = (col % settings.gridColumns) * (cellSize.width() + settings.spacing);
            drawY = (col / settings.gridColumns) * (cellSize.height() + settings.spacing);
            ++col;
        }
        painter.drawImage(drawX, drawY, img);
        if (inputRects)
            inputRects->append(QRect(drawX, drawY, img.width(), img.height()));
    }
    painter.end();

    if (settings.autoCropEdges) {
        result = cropWhiteEdges(result);
        // 裁剪后 rects 坐标失效，清空以保证高亮准确
        if (inputRects)
            inputRects->clear();
    }

    return result;
}

QSize StitchEngine::estimateOutputSize(const QStringList& filePaths, const StitchSettings& settings)
{
    return ::yingtu::estimateOutputSize(filePaths, settings);
}

} // namespace yingtu
