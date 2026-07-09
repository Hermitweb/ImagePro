#pragma once

#include <QImage>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace yingtu {

class PreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setSourcePath(const QString& path, int rotation = 0,
                       bool flippedHorizontal = false, bool flippedVertical = false);
    void clear();

    void setZoom(double factor);
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void resetZoom();

    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    void resetTransform();

    void setComparisonMode(bool enabled);
    bool comparisonMode() const { return m_comparisonMode; }
    void setOriginalImage(const QImage& image);

    QImage currentImage() const { return m_image; }
    QImage displayedImage();
    QSize viewportSize() const;

signals:
    void zoomChanged(double factor);
    void deleteCurrentRequested();
    void rotateCurrentRequested();
    void rotateCurrentRightRequested();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void updatePixmap();
    QImage baseImage();
    QImage transformedImage();
    QImage createComparisonImage(const QImage& original, const QImage& processed) const;
    QSize neededSourceSize() const;
    QImage loadSourcePreview(const QSize& targetSize);

    QScrollArea* m_scrollArea = nullptr;
    QLabel* m_imageLabel = nullptr;
    QImage m_image;
    QImage m_originalImage;
    QString m_sourcePath;
    QSize m_sourceSize;
    int m_sourceRotation = 0;
    bool m_sourceFlippedH = false;
    bool m_sourceFlippedV = false;
    double m_zoomFactor = 1.0;
    bool m_fitToWindow = true;
    bool m_comparisonMode = false;
    int m_rotation = 0;
    bool m_flippedH = false;
    bool m_flippedV = false;

    // 源图预览缓存，避免缩放/旋转时反复从磁盘解码
    QImage m_cachedSourcePreview;
    QSize m_cachedSourcePreviewSize;
    void invalidateSourcePreviewCache();
};

} // namespace yingtu
