#include "ImageItem.h"
#include "utils/ImageLoader.h"
#include <QFileInfo>
#include <QImageReader>
#include <QTransform>
#include <QUuid>

namespace yingtu {

ImageItem::ImageItem(const QString& filePath, bool loadInfoImmediately)
    : m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_filePath(filePath),
      m_loadState(LoadState::Loading)
{
    if (loadInfoImmediately && !filePath.isEmpty())
        reloadInfo();
}

QString ImageItem::displayName() const
{
    return QFileInfo(m_filePath).fileName();
}

QString ImageItem::resolutionString() const
{
    if (m_width <= 0 || m_height <= 0)
        return QString();
    return QStringLiteral("%1×%2").arg(m_width).arg(m_height);
}

void ImageItem::setRotation(int deg)
{
    int newRotation = deg % 360;
    if (newRotation < 0)
        newRotation += 360;
    if (m_rotation != newRotation) {
        m_rotation = newRotation;
        // 只让「变换后」的缓存失效：原始解码结果保留，
        // 下次绘制重新做一次几何变换即可，无需重新解码文件。
        m_thumbnailCache.clear();
    }
}

void ImageItem::rotate90()
{
    setRotation(m_rotation + 90);
}

void ImageItem::setFlippedHorizontal(bool flipped)
{
    if (m_flippedHorizontal != flipped) {
        m_flippedHorizontal = flipped;
        // 翻转会改变缩略图外观，必须让缓存失效，否则列表仍显示旧图。
        m_thumbnailCache.clear();
    }
}

void ImageItem::setFlippedVertical(bool flipped)
{
    if (m_flippedVertical != flipped) {
        m_flippedVertical = flipped;
        m_thumbnailCache.clear();
    }
}

QPixmap ImageItem::thumbnail(int size) const
{
    if (!m_valid)
        return QPixmap();

    auto it = m_thumbnailCache.find(size);
    if (it != m_thumbnailCache.end() && !it->isNull())
        return *it;

    // 变换后缓存未命中，但原始解码结果还在：只需重新做一次几何变换。
    // 这是旋转/翻转后能立即显示新方向、且不产生卡顿的关键。
    auto rawIt = m_rawThumbnailCache.find(size);
    if (rawIt != m_rawThumbnailCache.end() && !rawIt->isNull()) {
        const QPixmap pix = QPixmap::fromImage(applyTransform(rawIt.value()));
        m_thumbnailCache.insert(size, pix);
        return pix;
    }

    // 完全没有缓存时**不**在这里解码。
    // 本函数位于 data() → paintEvent 链路（UI 线程），同步解码大图会造成可见卡顿；
    // 异步加载路径会补上缩略图，补上之前显示占位图即可。
    return QPixmap();
}

void ImageItem::setThumbnail(const QPixmap& pixmap)
{
    int size = pixmap.isNull() ? 0 : pixmap.width();
    setThumbnail(size, pixmap);
}

void ImageItem::setThumbnail(int size, const QPixmap& pixmap)
{
    m_thumbnailCache.insert(size, pixmap);
}

void ImageItem::setDecodedThumbnail(int size, const QImage& image)
{
    if (image.isNull())
        return;
    // 原始解码结果单独缓存：旋转/翻转后只需重新变换，不必重新解码文件。
    m_rawThumbnailCache.insert(size, image);
    // 必须先应用旋转/翻转再入缓存：thumbnail() 命中缓存时不会再做变换，
    // 否则对已旋转/翻转的图片，异步加载完成会把未变换的缩略图写进缓存，
    // 列表就一直显示旧方向。
    m_thumbnailCache.insert(size, QPixmap::fromImage(applyTransform(image)));
}

void ImageItem::copyThumbnailFrom(const ImageItem& other, int size)
{
    auto transformed = other.m_thumbnailCache.constFind(size);
    if (transformed != other.m_thumbnailCache.constEnd() && !transformed->isNull())
        m_thumbnailCache.insert(size, transformed.value());

    auto raw = other.m_rawThumbnailCache.constFind(size);
    if (raw != other.m_rawThumbnailCache.constEnd() && !raw->isNull())
        m_rawThumbnailCache.insert(size, raw.value());
}

bool ImageItem::hasThumbnail(int size) const
{
    auto it = m_thumbnailCache.find(size);
    if (it != m_thumbnailCache.end() && !it->isNull())
        return true;
    // 原始解码结果存在也算有：旋转/翻转后可直接由它重新变换得到。
    auto raw = m_rawThumbnailCache.find(size);
    return raw != m_rawThumbnailCache.end() && !raw->isNull();
}

QImage ImageItem::loadImage() const
{
    QImage img = ImageLoader::loadImage(m_filePath);
    return applyTransform(img);
}

QImage ImageItem::loadPreviewImage(const QSize& maxSize) const
{
    if (maxSize.isEmpty())
        return loadImage();

    QImage img = ImageLoader::loadPreview(m_filePath, maxSize);
    return applyTransform(img);
}

QImage ImageItem::applyTransform(const QImage& img) const
{
    if (img.isNull())
        return img;

    // 90° 的整数倍旋转是像素的精确重排，不需要双线性插值：
    // 用 FastTransformation 既更快也更清晰（不会因插值而发虚）。
    // 其余角度仍用平滑插值，避免出现锯齿。
    const bool quarterTurn = (m_rotation % 90) == 0;
    const Qt::TransformationMode mode =
        quarterTurn ? Qt::FastTransformation : Qt::SmoothTransformation;

    QTransform transform;
    if (m_rotation != 0)
        transform.rotate(m_rotation);
    if (m_flippedHorizontal || m_flippedVertical) {
        transform.scale(m_flippedHorizontal ? -1 : 1, m_flippedVertical ? -1 : 1);
    }

    if (!transform.isIdentity())
        return img.transformed(transform, mode);
    return img;
}

void ImageItem::reloadInfo()
{
    setInfo(ImageLoader::loadInfo(m_filePath));
}

void ImageItem::retryLoad()
{
    m_loadState = LoadState::Loading;
    m_thumbnailCache.clear();
    reloadInfo();
}

void ImageItem::setInfo(const ImageInfo& info)
{
    m_valid = info.valid;
    m_width = info.width;
    m_height = info.height;
    m_fileSize = info.fileSize;
    m_format = info.format;
    m_loadState = info.valid ? LoadState::Ready : LoadState::Failed;
}

} // namespace yingtu
