#include "ImageItem.h"
#include "utils/ImageLoader.h"
#include <QFileInfo>
#include <QImageReader>
#include <QTransform>
#include <QUuid>

namespace yingtu {

ImageItem::ImageItem(const QString& filePath, bool loadInfoImmediately)
    : m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_filePath(filePath)
{
    if (loadInfoImmediately && !filePath.isEmpty())
        reloadInfo();
}

QString ImageItem::displayName() const
{
    return QFileInfo(m_filePath).fileName();
}

void ImageItem::setRotation(int deg)
{
    int newRotation = deg % 360;
    if (newRotation < 0)
        newRotation += 360;
    if (m_rotation != newRotation) {
        m_rotation = newRotation;
        // 旋转变化后缩略图缓存失效
        m_thumbnailCache.clear();
    }
}

void ImageItem::rotate90()
{
    setRotation(m_rotation + 90);
}

QPixmap ImageItem::thumbnail(int size) const
{
    if (!m_valid)
        return QPixmap();

    auto it = m_thumbnailCache.find(size);
    if (it != m_thumbnailCache.end() && !it->isNull())
        return *it;

    QPixmap pix = ImageLoader::loadThumbnail(m_filePath, size);
    m_thumbnailCache.insert(size, pix);
    return pix;
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

bool ImageItem::hasThumbnail(int size) const
{
    auto it = m_thumbnailCache.find(size);
    return it != m_thumbnailCache.end() && !it->isNull();
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

    QTransform transform;
    if (m_rotation != 0)
        transform.rotate(m_rotation);
    if (m_flippedHorizontal || m_flippedVertical) {
        transform.scale(m_flippedHorizontal ? -1 : 1, m_flippedVertical ? -1 : 1);
    }

    if (!transform.isIdentity())
        return img.transformed(transform, Qt::SmoothTransformation);
    return img;
}

void ImageItem::reloadInfo()
{
    setInfo(ImageLoader::loadInfo(m_filePath));
}

void ImageItem::setInfo(const ImageInfo& info)
{
    m_valid = info.valid;
    m_width = info.width;
    m_height = info.height;
    m_fileSize = info.fileSize;
    m_format = info.format;
}

} // namespace yingtu
