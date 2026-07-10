#pragma once

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace yingtu {

enum class EditToolType {
    Rectangle,
    Ellipse,
    Arrow,
    Pen,
    Mosaic,
    Text,
    Crop,
    Filter
};

enum class EditFillStyle {
    NoFill,
    SemiFill,
    SolidFill
};

enum class FilterType {
    Grayscale,
    Sepia,
    Warm,
    Cool,
    HighContrast,
    Blur,
    Sharpen
};

struct EditAction {
    QString id;
    EditToolType toolType = EditToolType::Rectangle;
    FilterType filterType = FilterType::Grayscale;
    QColor color = Qt::red;
    int lineWidth = 3;
    int opacity = 80; // 0~100
    int fontSize = 16;
    QString fontFamily;
    EditFillStyle fillStyle = EditFillStyle::SemiFill;
    QList<QPointF> points;
    QString text;
    QRectF bounds;
    QDateTime timestamp;

    bool isMovable() const {
        return toolType == EditToolType::Rectangle ||
               toolType == EditToolType::Ellipse ||
               toolType == EditToolType::Arrow ||
               toolType == EditToolType::Text ||
               toolType == EditToolType::Crop;
    }

    bool isFilter() const {
        return toolType == EditToolType::Filter;
    }
};

struct SelectionState {
    QString selectedActionId;
    int activeHandle = -1; // -1 表示移动整体，0~7 表示控制点
    QPointF dragStartPos;
    QRectF originalBounds;
};

} // namespace yingtu
