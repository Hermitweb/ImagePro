#pragma once

#include "core/EditEngine.h"
#include "utils/EditAction.h"
#include <QImage>
#include <QWidget>

namespace yingtu {

class ImageEditorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImageEditorWidget(QWidget* parent = nullptr);

    void setBaseImage(const QImage& image);
    void setCurrentTool(const EditAction& actionTemplate);
    void clearActions();
    void undo();
    void redo();
    void jumpToHistoryIndex(int index);

    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitToWindow();
    double zoomFactor() const { return m_zoomFactor; }

    void rotateLeft();
    void rotateRight();
    void resetTransform();

    QImage renderedImage() const;
    QList<EditAction> history() const { return m_history; }

signals:
    void actionAdded(const EditAction& action);
    void selectionChanged(const QString& actionId);
    void historyChanged(const QList<EditAction>& history, int currentIndex);

signals:
    void zoomChanged(double factor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QPointF mapToImage(const QPoint& pos) const;
    QPoint mapFromImage(const QPointF& pos) const;
    QSize rotatedImageSize() const;
    QTransform viewTransform() const;
    QTransform imageTransform() const;
    QRectF effectiveCropBounds() const;
    QPointF cropOffset() const;
    void updateCanvas();
    void clampPan();
    void finishCurrentAction();
    void applyRotation(int delta);
    EditEngine m_engine;
    QImage m_originalBaseImage;
    EditAction m_currentTemplate;
    EditAction m_currentAction;
    bool m_drawing = false;
    SelectionState m_selection;
    QList<EditAction> m_history;
    int m_historyIndex = -1;

    // 渲染缓存：仅在底图、标注或选中项变化时重绘，避免每帧复制大图
    QImage m_canvasCache;
    QString m_cachedSelectionId;
    bool m_canvasCacheDirty = true;

    // 视图变换：缩放、平移、旋转
    double m_zoomFactor = 1.0;
    bool m_fitToWindow = true;
    QPointF m_panOffset;
    int m_rotation = 0; // 0, 90, 180, 270
    bool m_panning = false;
    QPoint m_panStartPos;
    QPointF m_panStartOffset;
};

} // namespace yingtu
