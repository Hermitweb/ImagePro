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

    QImage renderedImage() const;
    QList<EditAction> history() const { return m_history; }

signals:
    void actionAdded(const EditAction& action);
    void selectionChanged(const QString& actionId);
    void historyChanged(const QList<EditAction>& history, int currentIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QPointF mapToImage(const QPoint& pos) const;
    void updateCanvas();
    void finishCurrentAction();
    EditEngine m_engine;
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
};

} // namespace yingtu
