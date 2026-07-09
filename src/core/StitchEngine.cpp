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

static bool isPixelWhite(const QColor& c, int threshold = 250)
{
    return c.red() >= threshold && c.green() >= threshold && c.blue() >= threshold;
}

static QImage cropWhiteEdges(const QImage& source, int threshold = 250)
{
    if (source.isNull())
        return source;

    int left = 0, top = 0, right = source.width() - 1, bottom = source.height() - 1;
    bool found = false;

    for (int y = 0; y < source.height(); ++y) {
        bool rowHasContent = false;
        for (int x = 0; x < source.width(); ++x) {
            if (!isPixelWhite(QColor(source.pixel(x, y)), threshold)) {
                rowHasContent = true;
                break;
            }
        }
        if (rowHasContent) {
            top = y;
            found = true;
            break;
        }
    }
    if (!found)
        return source;

    for (int y = source.height() - 1; y >= 0; --y) {
        bool rowHasContent = false;
        for (int x = 0; x < source.width(); ++x) {
            if (!isPixelWhite(QColor(source.pixel(x, y)), threshold)) {
                rowHasContent = true;
                break;
            }
        }
        if (rowHasContent) {
            bottom = y;
            break;
        }
    }

    for (int x = 0; x < source.width(); ++x) {
        bool colHasContent = false;
        for (int y = top; y <= bottom; ++y) {
            if (!isPixelWhite(QColor(source.pixel(x, y)), threshold)) {
                colHasContent = true;
                break;
            }
        }
        if (colHasContent) {
            left = x;
            break;
        }
    }

    for (int x = source.width() - 1; x >= 0; --x) {
        bool colHasContent = false;
        for (int y = top; y <= bottom; ++y) {
            if (!isPixelWhite(QColor(source.pixel(x, y)), threshold)) {
                colHasContent = true;
                break;
            }
        }
        if (colHasContent) {
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
    return vips_image_new_from_file(path.toUtf8().constData(),
                                    "access", VIPS_ACCESS_SEQUENTIAL,
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

static QList<VipsImageInfo> loadAndPrepareImages(const QStringList& filePaths,
                                                  const StitchSettings& settings,
                                                  int maxLongEdge)
{
    QList<VipsImageInfo> infos;

    // 先统一获取原始尺寸，计算目标尺寸
    QList<QSize> originalSizes;
    for (const QString& path : filePaths) {
        ImageInfo info = ImageLoader::loadInfo(path);
        if (info.valid)
            originalSizes.append(QSize(info.width, info.height));
        else
            originalSizes.append(QSize());
    }

    int targetCellW = 0;
    int targetCellH = 0;
    if (settings.direction == StitchSettings::Grid) {
        for (const QSize& s : originalSizes) {
            if (!s.isEmpty()) {
                targetCellW = qMax(targetCellW, s.width());
                targetCellH = qMax(targetCellH, s.height());
            }
        }
    } else if (settings.direction == StitchSettings::Vertical && settings.uniformWidth) {
        for (const QSize& s : originalSizes) {
            if (!s.isEmpty())
                targetCellW = qMax(targetCellW, s.width());
        }
    } else if (settings.direction == StitchSettings::Horizontal && settings.uniformHeight) {
        for (const QSize& s : originalSizes) {
            if (!s.isEmpty())
                targetCellH = qMax(targetCellH, s.height());
        }
    }

    for (int i = 0; i < filePaths.size(); ++i) {
        VipsImage* img = loadVipsImage(filePaths.at(i));
        if (!img)
            continue;

        const QSize orig = originalSizes.at(i);
        if (orig.isEmpty()) {
            g_object_unref(img);
            continue;
        }

        int displayW = orig.width();
        int displayH = orig.height();

        // 预览模式下限制长边
        if (maxLongEdge > 0) {
            int longEdge = qMax(displayW, displayH);
            if (longEdge > maxLongEdge) {
                double scale = static_cast<double>(maxLongEdge) / longEdge;
                displayW = qRound(displayW * scale);
                displayH = qRound(displayH * scale);
            }
        }

        VipsImage* scaled = img;
        if (settings.direction == StitchSettings::Vertical && settings.uniformWidth && targetCellW > 0) {
            VipsImage* tmp = resizeVipsImage(img, targetCellW, qRound(orig.height() * (static_cast<double>(targetCellW) / orig.width())), true);
            if (tmp) {
                g_object_unref(scaled);
                scaled = tmp;
            }
        } else if (settings.direction == StitchSettings::Horizontal && settings.uniformHeight && targetCellH > 0) {
            VipsImage* tmp = resizeVipsImage(img, qRound(orig.width() * (static_cast<double>(targetCellH) / orig.height())), targetCellH, true);
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
        } else if (maxLongEdge > 0) {
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

static VipsImage* stitchVipsImages(const QList<VipsImageInfo>& infos, const StitchSettings& settings)
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
    int x = 0, y = 0;
    int col = 0;

    int cellW = 0, cellH = 0;
    if (settings.direction == StitchSettings::Grid) {
        for (const auto& info : infos) {
            cellW = qMax(cellW, info.displayWidth);
            cellH = qMax(cellH, info.displayHeight);
        }
    }

    for (int i = 0; i < infos.size(); ++i) {
        const auto& info = infos.at(i);
        if (settings.direction == StitchSettings::Horizontal) {
            x += info.displayWidth + settings.spacing;
            int px = x - info.displayWidth - settings.spacing;
            int py = (totalHeight - info.displayHeight) / 2;
        } else if (settings.direction == StitchSettings::Vertical) {
            y += info.displayHeight + settings.spacing;
            int px = (totalWidth - info.displayWidth) / 2;
            int py = y - info.displayHeight - settings.spacing;
        } else {
            x = (col % settings.gridColumns) * (cellW + settings.spacing);
            y = (col / settings.gridColumns) * (cellH + settings.spacing);
            ++col;
        }

        VipsImage* inserted = nullptr;
        if (vips_insert(current, info.image, &inserted,
                        settings.direction == StitchSettings::Horizontal ? (x - info.displayWidth - settings.spacing) : x,
                        settings.direction == StitchSettings::Horizontal ? (totalHeight - info.displayHeight) / 2 : y,
                        nullptr)) {
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

static QImage vipsStitchPreview(const QStringList& filePaths, const StitchSettings& settings)
{
    QList<VipsImageInfo> infos = loadAndPrepareImages(filePaths, settings, 800);
    VipsImage* stitched = stitchVipsImages(infos, settings);
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

    QString dir = m_settings.outputDir;
    if (dir.isEmpty())
        dir = QFileInfo(filePaths.first()).absolutePath();

    QString suffix = QStringLiteral(".") + m_settings.outputFormat.toLower();
    QString outputPath = FileUtils::generateUniqueOutputPath(dir, m_settings.baseName, suffix);

#ifdef USE_LIBVIPS
    if (!m_settings.removeWhiteEdges && !m_settings.autoCropEdges) {
        if (vipsStitchProcess(filePaths, m_settings, outputPath)) {
            if (ok) *ok = true;
            emit finished(outputPath);
            return outputPath;
        }
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

QImage StitchEngine::preview(const QStringList& filePaths, const StitchSettings& settings)
{
#ifdef USE_LIBVIPS
    if (!settings.removeWhiteEdges) {
        QImage img = vipsStitchPreview(filePaths, settings);
        if (!img.isNull())
            return img;
    }
#endif

    QList<QImage> images;

    for (const QString& path : filePaths) {
        QImage img = ImageLoader::loadImage(path);
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
    int x = 0, y = 0;
    int col = 0;

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
        if (settings.direction == StitchSettings::Horizontal) {
            painter.drawImage(x, 0, img);
            x += img.width() + settings.spacing;
        } else if (settings.direction == StitchSettings::Vertical) {
            painter.drawImage(0, y, img);
            y += img.height() + settings.spacing;
        } else {
            int cx = (col % settings.gridColumns) * (cellSize.width() + settings.spacing);
            int cy = (col / settings.gridColumns) * (cellSize.height() + settings.spacing);
            painter.drawImage(cx, cy, img);
            ++col;
        }
    }
    painter.end();

    if (settings.autoCropEdges)
        result = cropWhiteEdges(result);

    return result;
}

} // namespace yingtu
