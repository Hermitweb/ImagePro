#pragma once

#include "utils/ImageLoader.h"
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QString>

namespace yingtu {

class ImageItem
{
public:
    enum class LoadState {
        Loading,
        Ready,
        Failed
    };

    ImageItem() = default;
    explicit ImageItem(const QString& filePath, bool loadInfoImmediately = true);

    QString id() const { return m_id; }
    QString filePath() const { return m_filePath; }
    QString displayName() const;
    QString resolutionString() const;

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

    bool isHidden() const { return m_hidden; }
    void setHidden(bool hidden) { m_hidden = hidden; }

    LoadState loadState() const { return m_loadState; }
    void setLoadState(LoadState state) { m_loadState = state; }

    void setInfo(const ImageInfo& info);
    void setThumbnail(const QPixmap& pixmap);
    void setThumbnail(int size, const QPixmap& pixmap);
    // 缓存一张「原始解码」的缩略图（异步加载路径使用）：内部会先应用旋转/翻转，
    // 与 thumbnail() 的缓存语义保持一致。
    void setDecodedThumbnail(int size, const QImage& image);
    bool hasThumbnail(int size) const;
    QPixmap thumbnail(int size) const;

    /**
     * 复制另一条目的缩略图缓存（原始解码结果与变换后结果都复制）。
     * 用于「复制图片」后立即显示相同缩略图，而不必等待异步重新加载；
     * 同时保证副本之后再旋转/翻转时仍能复用原始解码结果。
     */
    void copyThumbnailFrom(const ImageItem& other, int size);

    void rotate90();
    void setRotation(int deg);
    // 必须在 .cpp 中实现：翻转同样会让缩略图缓存失效（原先的内联 setter
    // 不清理缓存，导致列表缩略图永远不反映翻转结果）。
    void setFlippedHorizontal(bool flipped);
    void setFlippedVertical(bool flipped);

    void reloadInfo();
    void retryLoad();
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
    bool m_hidden = false;
    LoadState m_loadState = LoadState::Loading;
    // 原始解码的缩略图（未应用旋转/翻转）。
    //
    // 单独缓存的意义：旋转/翻转只需要重做一次几何变换，不必重新解码文件。
    // 原先两种缓存合二为一，变换一改就把解码结果也丢掉，
    // 下一次绘制只能在 UI 线程同步解码大图 —— 明显的卡顿来源。
    mutable QHash<int, QImage> m_rawThumbnailCache;
    // 已应用当前旋转/翻转的缩略图
    mutable QHash<int, QPixmap> m_thumbnailCache;
    QDateTime m_lastModified;
};

} // namespace yingtu
