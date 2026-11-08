#pragma once

#include <QImage>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace yingtu {

class ImageListModel;
class StitchCanvas;

class PreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setSourcePath(const QString& path, int rotation = 0, bool flippedHorizontal = false,
                       bool flippedVertical = false);
    /**
     * 带已知尺寸的版本：调用方（模型异步加载过宽高）直接传入尺寸，
     * 避免每次切换选中图都在 GUI 线程读文件头（网络盘上可感知卡顿）。
     * knownSize 无效时回退为读头。
     */
    void setSourcePath(const QString& path, const QSize& knownSize, int rotation = 0,
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

    void setStitchMode(bool enabled);
    bool stitchMode() const { return m_stitchMode; }
    void setStitchSynthesizedImage(const QImage& image);
    void setStitchInputRects(const QVector<QRect>& rects);
    void setStitchHighlightedInput(int index);
    void resetStitchCanvas();
    void setStitchImageListModel(ImageListModel* model);

    QImage currentImage() const { return m_image; }
    QImage displayedImage();
    QSize viewportSize() const;

signals:
    void zoomChanged(double factor);
    void deleteCurrentRequested();
    void rotateCurrentRequested();
    void rotateCurrentRightRequested();

    // 转发 StitchCanvas 信号
    void stitchInputImageClicked(int index);
    void stitchInputImageDoubleClicked(int index);
    void stitchRotateInputImageRequested(int index, bool left);
    void stitchFlipInputImageHorizontalRequested(int index);
    void stitchFlipInputImageVerticalRequested(int index);
    void stitchRemoveInputImageRequested(int index);
    void stitchInputImageInfoRequested(int index);
    void stitchImageDropped(const QStringList& paths);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // 语言切换：重新套用常驻文本。
    void changeEvent(QEvent* event) override;
    // 视口事件过滤：拖拽平移与双击适应窗口。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updatePixmap();
    QImage baseImage();
    QImage transformedImage();
    QImage createComparisonImage(const QImage& original, const QImage& processed) const;
    QSize neededSourceSize() const;
    QImage loadSourcePreview(const QSize& targetSize);

    QScrollArea* m_scrollArea = nullptr;
    QLabel* m_imageLabel = nullptr;
    StitchCanvas* m_stitchCanvas = nullptr;
    QImage m_image;
    QImage m_originalImage;
    QString m_sourcePath;
    QSize m_sourceSize;
    int m_sourceRotation = 0;
    bool m_sourceFlippedH = false;
    bool m_sourceFlippedV = false;
    double m_zoomFactor = 1.0;
    bool m_fitToWindow = true;
    // 拖拽平移状态（中键/左键按住拖动滚动条）
    bool m_panning = false;
    QPoint m_panStartPos;
    int m_panStartH = 0;
    int m_panStartV = 0;
    bool m_comparisonMode = false;
    bool m_stitchMode = false;
    int m_rotation = 0;
    bool m_flippedH = false;
    bool m_flippedV = false;

    // 源图预览缓存，避免缩放/旋转时反复从磁盘解码
    QImage m_cachedSourcePreview;
    QSize m_cachedSourcePreviewSize;
    void invalidateSourcePreviewCache();
};

} // namespace yingtu
