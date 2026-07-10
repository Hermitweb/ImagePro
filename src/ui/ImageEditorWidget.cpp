#include "ImageEditorWidget.h"
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QUuid>

namespace yingtu {

ImageEditorWidget::ImageEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

void ImageEditorWidget::setBaseImage(const QImage& image)
{
    m_engine.setBaseImage(image);
    m_canvasCacheDirty = true;
    updateCanvas();
}

void ImageEditorWidget::setCurrentTool(const EditAction& actionTemplate)
{
    m_currentTemplate = actionTemplate;
    update();
}

void ImageEditorWidget::clearActions()
{
    m_engine.clearActions();
    m_history.clear();
    m_historyIndex = -1;
    emit historyChanged(m_history, m_historyIndex);
    updateCanvas();
}

void ImageEditorWidget::undo()
{
    if (m_historyIndex > 0) {
        --m_historyIndex;
        QList<EditAction> actions;
        for (int i = 0; i <= m_historyIndex; ++i)
            actions.append(m_history[i]);
        m_engine.setActions(actions);
        emit historyChanged(m_history, m_historyIndex);
        updateCanvas();
    }
}

void ImageEditorWidget::redo()
{
    if (m_historyIndex < m_history.size() - 1) {
        ++m_historyIndex;
        QList<EditAction> actions;
        for (int i = 0; i <= m_historyIndex; ++i)
            actions.append(m_history[i]);
        m_engine.setActions(actions);
        emit historyChanged(m_history, m_historyIndex);
        updateCanvas();
    }
}

void ImageEditorWidget::jumpToHistoryIndex(int index)
{
    if (index < 0 || index >= m_history.size())
        return;
    m_historyIndex = index;
    QList<EditAction> actions;
    for (int i = 0; i <= m_historyIndex; ++i)
        actions.append(m_history[i]);
    m_engine.setActions(actions);
    m_selection.selectedActionId.clear();
    emit selectionChanged(QString());
    emit historyChanged(m_history, m_historyIndex);
    updateCanvas();
}

QImage ImageEditorWidget::renderedImage() const
{
    return m_engine.render();
}

void ImageEditorWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_canvasCacheDirty || m_cachedSelectionId != m_selection.selectedActionId) {
        m_canvasCache = m_engine.renderWithSelection(m_selection.selectedActionId);
        m_cachedSelectionId = m_selection.selectedActionId;
        m_canvasCacheDirty = false;
    }

    QRect target = rect();
    if (!m_canvasCache.isNull())
        painter.drawImage(target, m_canvasCache);
    else
        painter.fillRect(rect(), QColor(245, 247, 250));
}

void ImageEditorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    QPointF pos = mapToImage(event->pos());

    // Filter tool applies immediately on click
    if (m_currentTemplate.toolType == EditToolType::Filter) {
        m_selection.selectedActionId.clear();
        emit selectionChanged(QString());

        EditAction filterAction = m_currentTemplate;
        filterAction.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        filterAction.timestamp = QDateTime::currentDateTime();
        filterAction.bounds = QRectF(pos, QSizeF(0, 0));
        m_engine.addAction(filterAction);
        emit actionAdded(filterAction);

        while (m_history.size() > m_historyIndex + 1)
            m_history.removeLast();
        m_history.append(filterAction);
        m_historyIndex = m_history.size() - 1;
        emit historyChanged(m_history, m_historyIndex);
        updateCanvas();
        return;
    }

    // 先检查是否点中已有标注
    const auto actions = m_engine.actions();
    for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
        if (!it->isMovable())
            continue;
        int hit = EditEngine::hitTest(it->bounds, pos);
        if (hit >= -1) {
            m_selection.selectedActionId = it->id;
            m_selection.activeHandle = hit;
            m_selection.dragStartPos = pos;
            m_selection.originalBounds = it->bounds;
            emit selectionChanged(m_selection.selectedActionId);
            update();
            return;
        }
    }

    m_selection.selectedActionId.clear();
    emit selectionChanged(QString());

    m_currentAction = m_currentTemplate;
    m_currentAction.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_currentAction.timestamp = QDateTime::currentDateTime();
    m_currentAction.points.append(pos);
    m_currentAction.bounds = QRectF(pos, QSizeF(0, 0));
    m_drawing = true;
}

void ImageEditorWidget::mouseMoveEvent(QMouseEvent* event)
{
    QPointF pos = mapToImage(event->pos());

    if (!m_selection.selectedActionId.isEmpty()) {
        QPointF delta = pos - m_selection.dragStartPos;
        auto action = m_engine.actions();
        for (auto& a : action) {
            if (a.id != m_selection.selectedActionId)
                continue;

            if (m_selection.activeHandle == -1) {
                a.bounds = m_selection.originalBounds.translated(delta);
            } else {
                QRectF b = m_selection.originalBounds;
                switch (m_selection.activeHandle) {
                case 0: b.setTopLeft(b.topLeft() + delta); break;
                case 1: b.setTop(b.top() + delta.y()); break;
                case 2: b.setTopRight(b.topRight() + delta); break;
                case 3: b.setLeft(b.left() + delta.x()); break;
                case 4: b.setRight(b.right() + delta.x()); break;
                case 5: b.setBottomLeft(b.bottomLeft() + delta); break;
                case 6: b.setBottom(b.bottom() + delta.y()); break;
                case 7: b.setBottomRight(b.bottomRight() + delta); break;
                }
                a.bounds = b;
            }
            m_engine.updateAction(a);
            break;
        }
        update();
        return;
    }

    if (!m_drawing)
        return;

    m_currentAction.points.append(pos);
    QRectF bounds = m_currentAction.bounds;
    bounds.setRight(qMax(bounds.right(), pos.x()));
    bounds.setBottom(qMax(bounds.bottom(), pos.y()));
    bounds.setLeft(qMin(bounds.left(), pos.x()));
    bounds.setTop(qMin(bounds.top(), pos.y()));
    m_currentAction.bounds = bounds;
    update();
}

void ImageEditorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
    if (!m_selection.selectedActionId.isEmpty()) {
        m_selection.activeHandle = -1;
        return;
    }

    if (!m_drawing)
        return;

    m_drawing = false;
    finishCurrentAction();
}

void ImageEditorWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    QPointF pos = mapToImage(event->pos());
    const auto actions = m_engine.actions();
    for (const auto& a : actions) {
        if (a.toolType == EditToolType::Text && a.bounds.contains(pos)) {
            bool ok = false;
            QString text = QInputDialog::getText(this, tr("Edit Text"), tr("Text:"),
                                                 QLineEdit::Normal, a.text, &ok);
            if (ok) {
                EditAction updated = a;
                updated.text = text;
                m_engine.updateAction(updated);
                updateCanvas();
            }
            return;
        }
    }
}

void ImageEditorWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete && !m_selection.selectedActionId.isEmpty()) {
        m_engine.removeAction(m_selection.selectedActionId);
        m_selection.selectedActionId.clear();
        emit selectionChanged(QString());
        updateCanvas();
    }
}

QPointF ImageEditorWidget::mapToImage(const QPoint& pos) const
{
    QImage img = m_engine.baseImage();
    if (img.isNull())
        return QPointF(pos);

    double sx = double(img.width()) / width();
    double sy = double(img.height()) / height();
    return QPointF(pos.x() * sx, pos.y() * sy);
}

void ImageEditorWidget::updateCanvas()
{
    m_canvasCacheDirty = true;
    update();
}

void ImageEditorWidget::finishCurrentAction()
{
    if (m_currentAction.toolType == EditToolType::Text) {
        bool ok = false;
        QString text = QInputDialog::getText(this, tr("Input Text"), tr("Text:"),
                                             QLineEdit::Normal, m_currentTemplate.text, &ok);
        if (!ok || text.isEmpty())
            return;
        m_currentAction.text = text;
        m_currentAction.bounds = QRectF(m_currentAction.points.first(), QSizeF(100, 30));
    }

    m_engine.addAction(m_currentAction);
    emit actionAdded(m_currentAction);

    // 维护历史
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(m_currentAction);
    m_historyIndex = m_history.size() - 1;
    emit historyChanged(m_history, m_historyIndex);

    m_currentAction = EditAction();
    updateCanvas();
}

} // namespace yingtu
