#pragma once

#include "utils/ImageLoader.h"
#include <QDateTime>
#include <QHash>
#include <QPixmap>
#include <QString>

namespace yingtu {

class ImageItem
{
public:
    ImageItem() = default;
    explicit ImageItem(const QString& filePath, bool loadInfoImmediately = true);

    QString id() const { return m_id; }
    QString filePath() const { return m_filePath; }
    QString displayName() const;

    bool isValid() const { return m_valid; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    qint64 fileSize() const { return m_fileSize; }
    QString format() const { return m_format; }

    int rotation() const { return m_rotation; }
    bool flippedHorizontal() const { return m_flippedHorizontal; }
    bool flippedVertical() const { return m_flippedVertical; }
    bool isSelected() const { return m_selected; }
    void setSelected(bool selected) { m_selected = selected; }

    void setInfo(const ImageInfo& info);
    void setThumbnail(const QPixmap& pixmap);
    void setThumbnail(int size, const QPixmap& pixmap);
    bool hasThumbnail(int size) const;
    QPixmap thumbnail(int size) const;

    void rotate90();
    void setRotation(int deg);
    void setFlippedHorizontal(bool flipped) { m_flippedHorizontal = flipped; }
    void setFlippedVertical(bool flipped) { m_flippedVertical = flipped; }

    void reloadInfo();
    QImage loadPreviewImage(const QSize& maxSize) const;
    QImage loadImage() const;

private:
    QImage applyTransform(const QImage& img) const;
    QString generateId() const;

    QString m_id;
    QString m_filePath;
    bool m_valid = false;
    int m_width = 0;
    int m_height = 0;
    qint64 m_fileSize = 0;
    QString m_format;
    int m_rotation = 0;
    bool m_flippedHorizontal = false;
    bool m_flippedVertical = false;
    bool m_selected = false;
    mutable QHash<int, QPixmap> m_thumbnailCache;
    QDateTime m_lastModified;
};

} // namespace yingtu
