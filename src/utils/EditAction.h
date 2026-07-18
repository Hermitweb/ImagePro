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

enum class MosaicStyle {
    Square,        // 经典方块马赛克
    Hexagon,       // 六边形马赛克
    Circle,        // 圆形马赛克
    Blur,          // 动态/模糊（毛玻璃）马赛克
    Mezzotint,     // 铜版雕刻
    ColorHalftone  // 彩色半调
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
    MosaicStyle mosaicStyle = MosaicStyle::Square;
    QColor color = Qt::red;
    int lineWidth = 3;
    int opacity = 80; // 0~100
    int fontSize = 16;
    int mosaicSize = 20; // 马赛克块大小
    QString fontFamily;
    bool fontBold = false;
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
               toolType == EditToolType::Mosaic ||
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
