#include "EditEngine.h"
#include <QPainter>
#include <QtMath>

namespace yingtu {

EditEngine::EditEngine(QObject* parent)
    : QObject(parent)
{
}

void EditEngine::addAction(const EditAction& action)
{
    m_actions.append(action);
    emit actionsChanged();
}

void EditEngine::removeAction(const QString& id)
{
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it) {
        if (it->id == id) {
            m_actions.erase(it);
            emit actionsChanged();
            return;
        }
    }
}

void EditEngine::updateAction(const EditAction& action)
{
    for (auto& a : m_actions) {
        if (a.id == action.id) {
            a = action;
            emit actionsChanged();
            return;
        }
    }
}

void EditEngine::clearActions()
{
    if (m_actions.isEmpty())
        return;
    m_actions.clear();
    emit actionsChanged();
}

void EditEngine::setActions(const QList<EditAction>& actions)
{
    m_actions = actions;
    emit actionsChanged();
}

QImage EditEngine::render() const
{
    return renderWithSelection(QString());
}

QImage EditEngine::renderWithSelection(const QString& selectedId) const
{
    QImage result = m_baseImage.isNull() ? QImage(800, 600, QImage::Format_ARGB32) : m_baseImage.copy();
    if (result.isNull())
        result.fill(Qt::white);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const auto& action : m_actions) {
        drawAction(&painter, action);
        if (action.id == selectedId && action.isMovable()) {
            QPen pen(Qt::white);
            pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            painter.drawRect(action.bounds.adjusted(-2, -2, 2, 2));
        }
    }
    painter.end();
    return result;
}

void EditEngine::drawAction(QPainter* painter, const EditAction& action) const
{
    QColor c = action.color;
    c.setAlpha(255 * action.opacity / 100);
    QPen pen(c);
    pen.setWidth(action.lineWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);

    switch (action.toolType) {
    case EditToolType::Rectangle: {
        if (action.fillStyle == EditFillStyle::NoFill)
            painter->setBrush(Qt::NoBrush);
        else if (action.fillStyle == EditFillStyle::SemiFill)
            painter->setBrush(QColor(c.red(), c.green(), c.blue(), 30));
        else
            painter->setBrush(QColor(c.red(), c.green(), c.blue(), 255 * action.opacity / 100));
        painter->drawRect(action.bounds);
        break;
    }
    case EditToolType::Ellipse: {
        if (action.fillStyle == EditFillStyle::NoFill)
            painter->setBrush(Qt::NoBrush);
        else if (action.fillStyle == EditFillStyle::SemiFill)
            painter->setBrush(QColor(c.red(), c.green(), c.blue(), 30));
        else
            painter->setBrush(QColor(c.red(), c.green(), c.blue(), 255 * action.opacity / 100));
        painter->drawEllipse(action.bounds);
        break;
    }
    case EditToolType::Arrow: {
        if (action.points.size() >= 2) {
            QLineF line(action.points.first(), action.points.last());
            painter->drawLine(line);
            double angle = std::atan2(-line.dy(), line.dx());
            qreal arrowSize = action.lineWidth * 4;
            QPointF arrowP1 = line.p2() - QPointF(std::sin(angle + M_PI / 3) * arrowSize,
                                                  std::cos(angle + M_PI / 3) * arrowSize);
            QPointF arrowP2 = line.p2() - QPointF(std::sin(angle + M_PI - M_PI / 3) * arrowSize,
                                                  std::cos(angle + M_PI - M_PI / 3) * arrowSize);
            painter->drawPolygon(QPolygonF() << line.p2() << arrowP1 << arrowP2);
        }
        break;
    }
    case EditToolType::Pen:
    case EditToolType::Mosaic: {
        if (action.points.size() > 1) {
            if (action.toolType == EditToolType::Mosaic) {
                painter->setPen(QPen(Qt::gray, action.lineWidth * 3));
            }
            for (int i = 1; i < action.points.size(); ++i)
                painter->drawLine(action.points[i - 1], action.points[i]);
        }
        break;
    }
    case EditToolType::Text: {
        QFont font = painter->font();
        font.setFamily(action.fontFamily.isEmpty() ? QStringLiteral("Microsoft YaHei") : action.fontFamily);
        font.setPointSize(action.fontSize > 0 ? action.fontSize : 16);
        painter->setFont(font);
        painter->setBrush(Qt::NoBrush);
        painter->drawText(action.bounds.topLeft(), action.text);
        break;
    }
    case EditToolType::Crop: {
        QPen cropPen(Qt::blue);
        cropPen.setWidth(2);
        cropPen.setStyle(Qt::DashLine);
        painter->setPen(cropPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(action.bounds);
        break;
    }
    }
}

QRectF EditEngine::handleRect(const QRectF& bounds, int handleIndex)
{
    const qreal s = 6;
    switch (handleIndex) {
    case 0: return QRectF(bounds.left() - s, bounds.top() - s, s * 2, s * 2);
    case 1: return QRectF(bounds.center().x() - s, bounds.top() - s, s * 2, s * 2);
    case 2: return QRectF(bounds.right() - s, bounds.top() - s, s * 2, s * 2);
    case 3: return QRectF(bounds.left() - s, bounds.center().y() - s, s * 2, s * 2);
    case 4: return QRectF(bounds.right() - s, bounds.center().y() - s, s * 2, s * 2);
    case 5: return QRectF(bounds.left() - s, bounds.bottom() - s, s * 2, s * 2);
    case 6: return QRectF(bounds.center().x() - s, bounds.bottom() - s, s * 2, s * 2);
    case 7: return QRectF(bounds.right() - s, bounds.bottom() - s, s * 2, s * 2);
    }
    return QRectF();
}

int EditEngine::hitTest(const QRectF& bounds, const QPointF& pos, int handleSize)
{
    for (int i = 0; i < 8; ++i) {
        if (handleRect(bounds, i).adjusted(-handleSize / 2, -handleSize / 2, handleSize / 2, handleSize / 2)
                .contains(pos))
            return i;
    }
    if (bounds.contains(pos))
        return -1; // 整体移动
    return -2; // 未命中
}

} // namespace yingtu
