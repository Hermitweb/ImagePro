#pragma once

#include <QImage>
#include <QVector>
#include <QWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QTimer;
class QWheelEvent;
class QVariantAnimation;

namespace yingtu {

class ImageListModel;

class StitchCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit StitchCanvas(QWidget* parent = nullptr);
    ~StitchCanvas() override;

    void setImageListModel(ImageListModel* model);
    void setSynthesizedImage(const QImage& image);
    void setInputRects(const QVector<QRect>& rects);

    void reset();
    void setZoom(double factor);
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void resetZoom();
    void rotateLeft();
    void rotateRight();

    double zoomFactor() const { return m_zoomFactor; }

signals:
    void inputImageClicked(int index);
    void inputImageDoubleClicked(int index);
    void rotateInputImageRequested(int index, bool left);
    void flipInputImageHorizontalRequested(int index);
    void flipInputImageVerticalRequested(int index);
    void removeInputImageRequested(int index);
    void inputImageInfoRequested(int index);
    void imageDropped(const QStringList& paths);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    class SpotlightWindow;

    void updateTransform();
    QRectF computeImageRect() const;
    QRectF imageRect() const { return m_imageRect; }
    void updateDisplayData();
    QPointF widgetToImage(const QPointF& pos) const;
    QPointF imageToWidget(const QPointF& pos) const;
    QRectF mapRectToWidget(const QRect& rect) const;
    int inputIndexAt(const QPointF& imagePos) const;

public:
    void setHighlightedIndex(int index);

private:
    void setHoveredIndex(int index);
    void updateCursor();
    void ensurePanInBounds();
    void updateButtonBar();
    void updateState();

    void layoutButtonBar();
    void drawButtonBar(QPainter& painter);
    int buttonAt(const QPoint& pos) const;
    void executeButtonAction(int action);
    QString buttonTooltip(int action) const;

    void openSpotlight(int index, const QPointF& imagePos);
    void closeSpotlight();
    void updateSpotlight(const QPointF& imagePos);
    QImage loadOriginalImage(int index) const;

    static QImage limitImageSize(const QImage& image);

    ImageListModel* m_model = nullptr;
    QImage m_image;
    QImage m_originalImage;
    QSize m_synthesizedSize;
    QSize m_originalSynthesizedSize;
    double m_imageScale = 1.0;
    QVector<QRect> m_inputRects;
    QVector<QRect> m_originalInputRects;
    int m_viewRotation = 0;

    double m_zoomFactor = 1.0;
    QPoint m_panOffset;
    bool m_fitToWindow = true;
    QRectF m_imageRect;

    int m_hoveredIndex = -1;
    int m_highlightedIndex = -1;
    bool m_shiftPressed = false;

    bool m_panning = false;
    QPoint m_panStart;
    QPoint m_panOffsetStart;
    bool m_mayPan = false;

    bool m_draggingOver = false;

    bool m_buttonBarVisible = false;
    double m_buttonBarOpacity = 0.0;
    QRect m_buttonBarRect;
    QVector<QRect> m_buttonRects;
    int m_hoveredButtonIndex = -1;

    SpotlightWindow* m_spotlight = nullptr;

    QTimer* m_tooltipTimer = nullptr;
    QVariantAnimation* m_buttonBarFade = nullptr;

    static constexpr int s_viewportMargin = 32;
    static constexpr double s_minZoom = 0.1;
    static constexpr double s_maxZoom = 8.0;
};

} // namespace yingtu
