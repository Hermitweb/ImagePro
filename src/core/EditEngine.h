#pragma once

#include "utils/EditAction.h"
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>

namespace yingtu {

class EditEngine : public QObject
{
    Q_OBJECT
public:
    explicit EditEngine(QObject* parent = nullptr);

    void setBaseImage(const QImage& image) { m_baseImage = image; }
    QImage baseImage() const { return m_baseImage; }

    void addAction(const EditAction& action);
    void removeAction(const QString& id);
    void updateAction(const EditAction& action);
    void clearActions();
    void setActions(const QList<EditAction>& actions);

    QList<EditAction> actions() const { return m_actions; }

    QImage render() const;
    QImage renderWithSelection(const QString& selectedId) const;

    QRectF cropBounds() const;

    static QRectF handleRect(const QRectF& bounds, int handleIndex);
    static int hitTest(const QRectF& bounds, const QPointF& pos, int handleSize = 8);

signals:
    void actionsChanged();

private:
    void drawAction(QPainter* painter, const EditAction& action) const;

    QImage m_baseImage;
    QList<EditAction> m_actions;
};

} // namespace yingtu
