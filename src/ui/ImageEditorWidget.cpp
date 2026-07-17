#include "ImageEditorWidget.h"
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QUuid>

namespace yingtu {

namespace {

QPointF rotatePoint(const QPointF& p, const QSize& fromSize, int degrees)
{
    degrees = degrees % 360;
    if (degrees < 0)
        degrees += 360;
    switch (degrees) {
    case 90:
        return QPointF(p.y(), fromSize.width() - 1 - p.x());
    case 180:
        return QPointF(fromSize.width() - 1 - p.x(), fromSize.height() - 1 - p.y());
    case 270:
        return QPointF(fromSize.height() - 1 - p.y(), p.x());
    default:
        return p;
    }
}

QRectF rotateRect(const QRectF& r, const QSize& fromSize, int degrees)
{
    QPointF tl = rotatePoint(r.topLeft(), fromSize, degrees);
    QPointF br = rotatePoint(r.bottomRight(), fromSize, degrees);
    QRectF result;
    result.setLeft(qMin(tl.x(), br.x()));
    result.setRight(qMax(tl.x(), br.x()));
    result.setTop(qMin(tl.y(), br.y()));
    result.setBottom(qMax(tl.y(), br.y()));
    return result;
}

EditAction rotateAction(const EditAction& action, const QSize& fromSize, int degrees)
{
    EditAction result = action;
    for (QPointF& p : result.points)
        p = rotatePoint(p, fromSize, degrees);
    result.bounds = rotateRect(action.bounds, fromSize, degrees);
    return result;
}

} // namespace

ImageEditorWidget::ImageEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

void ImageEditorWidget::setBaseImage(const QImage& image)
{
    m_originalBaseImage = image;
    m_engine.setBaseImage(image);
    m_canvasCacheDirty = true;
    m_rotation = 0;
    m_panOffset = QPointF();
    fitToWindow();
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
    painter.fillRect(rect(), QColor(245, 247, 250));

    if (m_canvasCacheDirty || m_cachedSelectionId != m_selection.selectedActionId) {
        m_canvasCache = m_engine.renderWithSelection(m_selection.selectedActionId);
        m_cachedSelectionId = m_selection.selectedActionId;
        m_canvasCacheDirty = false;
    }

    if (!m_canvasCache.isNull()) {
        painter.setWorldTransform(viewTransform());
        painter.drawImage(QRectF(0, 0, m_canvasCache.width(), m_canvasCache.height()), m_canvasCache);
    }
}

void ImageEditorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier))) {
        m_panning = true;
        m_panStartPos = event->pos();
        m_panStartOffset = m_panOffset;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

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
    int handleSize = qMax(4, qRound(16.0 / m_zoomFactor));
    for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
        if (!it->isMovable())
            continue;
        int hit = EditEngine::hitTest(it->bounds, pos, handleSize);
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
    if (m_panning) {
        QPoint delta = event->pos() - m_panStartPos;
        m_panOffset = m_panStartOffset + QPointF(delta);
        clampPan();
        updateCanvas();
        return;
    }

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
    if (m_panning) {
        m_panning = false;
        unsetCursor();
        return;
    }

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

void ImageEditorWidget::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() == 0)
        return;

    m_fitToWindow = false;
    QPointF before = mapToImage(event->position().toPoint());
    if (event->angleDelta().y() > 0)
        m_zoomFactor = qMin(10.0, m_zoomFactor * 1.2);
    else
        m_zoomFactor = qMax(0.1, m_zoomFactor / 1.2);
    QPointF after = mapFromImage(before);
    m_panOffset += QPointF(event->position()) - after;
    clampPan();
    updateCanvas();
    emit zoomChanged(m_zoomFactor);
}

void ImageEditorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_fitToWindow)
        fitToWindow();
    else
        clampPan();
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

QSize ImageEditorWidget::rotatedImageSize() const
{
    QImage img = m_engine.baseImage();
    if (img.isNull())
        return QSize(800, 600);
    return img.size();
}

QTransform ImageEditorWidget::viewTransform() const
{
    QImage img = m_engine.baseImage();
    if (img.isNull())
        return QTransform();

    QTransform t;
    t.translate(m_panOffset.x(), m_panOffset.y());
    t.scale(m_zoomFactor, m_zoomFactor);
    return t;
}

QTransform ImageEditorWidget::imageTransform() const
{
    return viewTransform().inverted();
}

QPointF ImageEditorWidget::mapToImage(const QPoint& pos) const
{
    return imageTransform().map(QPointF(pos));
}

QPoint ImageEditorWidget::mapFromImage(const QPointF& pos) const
{
    return viewTransform().map(pos).toPoint();
}

void ImageEditorWidget::clampPan()
{
    QSize displaySize = rotatedImageSize();
    QRectF imageRect(0, 0, displaySize.width() * m_zoomFactor, displaySize.height() * m_zoomFactor);
    QRect viewport = rect();

    if (imageRect.width() <= viewport.width()) {
        m_panOffset.setX((viewport.width() - imageRect.width()) / 2.0);
    } else {
        m_panOffset.setX(qMin(0.0, qMax(double(viewport.width() - imageRect.width()), m_panOffset.x())));
    }

    if (imageRect.height() <= viewport.height()) {
        m_panOffset.setY((viewport.height() - imageRect.height()) / 2.0);
    } else {
        m_panOffset.setY(qMin(0.0, qMax(double(viewport.height() - imageRect.height()), m_panOffset.y())));
    }
}

void ImageEditorWidget::updateCanvas()
{
    m_canvasCacheDirty = true;
    update();
}

void ImageEditorWidget::zoomIn()
{
    m_fitToWindow = false;
    m_zoomFactor = qMin(10.0, m_zoomFactor * 1.2);
    QPoint center = rect().center();
    QPointF imageCenter = mapToImage(center);
    m_panOffset += QPointF(center) - mapFromImage(imageCenter);
    clampPan();
    updateCanvas();
    emit zoomChanged(m_zoomFactor);
}

void ImageEditorWidget::zoomOut()
{
    m_fitToWindow = false;
    m_zoomFactor = qMax(0.1, m_zoomFactor / 1.2);
    QPoint center = rect().center();
    QPointF imageCenter = mapToImage(center);
    m_panOffset += QPointF(center) - mapFromImage(imageCenter);
    clampPan();
    updateCanvas();
    emit zoomChanged(m_zoomFactor);
}

void ImageEditorWidget::resetZoom()
{
    m_fitToWindow = false;
    m_zoomFactor = 1.0;
    m_panOffset = QPointF();
    clampPan();
    updateCanvas();
    emit zoomChanged(m_zoomFactor);
}

void ImageEditorWidget::fitToWindow()
{
    QImage img = m_engine.baseImage();
    if (img.isNull()) {
        m_zoomFactor = 1.0;
        m_panOffset = QPointF();
        updateCanvas();
        emit zoomChanged(m_zoomFactor);
        return;
    }

    m_fitToWindow = true;
    QSize displaySize = rotatedImageSize();
    double sx = double(width()) / displaySize.width();
    double sy = double(height()) / displaySize.height();
    m_zoomFactor = qMin(sx, sy) * 0.98;
    m_panOffset = QPointF();
    clampPan();
    updateCanvas();
    emit zoomChanged(m_zoomFactor);
}

void ImageEditorWidget::rotateLeft()
{
    applyRotation(270);
}

void ImageEditorWidget::rotateRight()
{
    applyRotation(90);
}

void ImageEditorWidget::resetTransform()
{
    if (m_originalBaseImage.isNull())
        return;

    if (m_rotation != 0) {
        QSize fromSize = rotatedImageSize();
        int back = (360 - m_rotation) % 360;
        for (auto& action : m_history)
            action = rotateAction(action, fromSize, back);
        if (m_drawing)
            m_currentAction = rotateAction(m_currentAction, fromSize, back);
    }

    m_rotation = 0;
    m_engine.setBaseImage(m_originalBaseImage);
    QList<EditAction> currentActions;
    for (int i = 0; i <= m_historyIndex; ++i)
        currentActions.append(m_history[i]);
    m_engine.setActions(currentActions);

    m_selection.selectedActionId.clear();
    emit selectionChanged(QString());
    fitToWindow();
    emit historyChanged(m_history, m_historyIndex);
}

void ImageEditorWidget::applyRotation(int delta)
{
    if (m_originalBaseImage.isNull())
        return;

    QSize fromSize = rotatedImageSize();
    QImage rotated = m_engine.baseImage().transformed(QTransform().rotate(delta), Qt::SmoothTransformation);
    m_engine.setBaseImage(rotated);

    for (auto& action : m_history)
        action = rotateAction(action, fromSize, delta);
    if (m_drawing)
        m_currentAction = rotateAction(m_currentAction, fromSize, delta);

    QList<EditAction> currentActions;
    for (int i = 0; i <= m_historyIndex; ++i)
        currentActions.append(m_history[i]);
    m_engine.setActions(currentActions);

    m_rotation = (m_rotation + delta) % 360;
    m_selection.selectedActionId.clear();
    emit selectionChanged(QString());
    fitToWindow();
    emit historyChanged(m_history, m_historyIndex);
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
