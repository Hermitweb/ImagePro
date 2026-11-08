#include "EditEngine.h"
#include "core/MosaicFilter.h"
#include <QPainter>
#include <QtMath>
#include <functional>

namespace yingtu {

namespace {

QImage applyFilter(const QImage& source, FilterType type, int strength)
{
    if (source.isNull())
        return source;

    QImage img = source.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    // 强度归一：50 为此前的硬编码默认档（各分支按 50=原效果标定）。
    const double k = qBound(0, strength, 100) / 50.0;

    switch (type) {
    case FilterType::Grayscale: {
        // 强度 = 灰度混合比例：0 原图，50 全灰，100 全灰（>50 保持全灰）
        const double mix = qMin(1.0, k);
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                int gray = qRound(0.299 * qRed(p) + 0.587 * qGreen(p) + 0.114 * qBlue(p));
                line[x] = qRgba(qRound(qRed(p) * (1 - mix) + gray * mix),
                                qRound(qGreen(p) * (1 - mix) + gray * mix),
                                qRound(qBlue(p) * (1 - mix) + gray * mix), qAlpha(p));
            }
        }
        break;
    }
    case FilterType::Sepia: {
        const double mix = qMin(1.0, k);
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                int r = qRed(p), g = qGreen(p), b = qBlue(p);
                int tr = qMin(255, qRound(0.393 * r + 0.769 * g + 0.189 * b));
                int tg = qMin(255, qRound(0.349 * r + 0.686 * g + 0.168 * b));
                int tb = qMin(255, qRound(0.272 * r + 0.534 * g + 0.131 * b));
                line[x] = qRgba(qRound(r * (1 - mix) + tr * mix),
                                qRound(g * (1 - mix) + tg * mix),
                                qRound(b * (1 - mix) + tb * mix), qAlpha(p));
            }
        }
        break;
    }
    case FilterType::Warm: {
        const int dr = qRound(30 * k);
        const int db = qRound(20 * k);
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                int r = qMin(255, qRed(p) + dr);
                int g = qGreen(p);
                int b = qMax(0, qBlue(p) - db);
                line[x] = qRgba(r, g, b, qAlpha(p));
            }
        }
        break;
    }
    case FilterType::Cool: {
        const int dr = qRound(20 * k);
        const int db = qRound(30 * k);
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                int r = qMax(0, qRed(p) - dr);
                int g = qGreen(p);
                int b = qMin(255, qBlue(p) + db);
                line[x] = qRgba(r, g, b, qAlpha(p));
            }
        }
        break;
    }
    case FilterType::HighContrast: {
        const double factor = 1.0 + 0.5 * k;
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                auto adjust = [factor](int v) {
                    double d = (v - 128) * factor + 128;
                    return qBound(0, qRound(d), 255);
                };
                line[x] = qRgba(adjust(qRed(p)), adjust(qGreen(p)), adjust(qBlue(p)), qAlpha(p));
            }
        }
        break;
    }
    case FilterType::Blur: {
        img = source.convertToFormat(QImage::Format_ARGB32);
        // 盒式模糊半径随强度增长：50 → 3×3（原硬编码效果），100 → 9×9。
        const int radius = qMax(1, qRound(k));
        QImage blurred(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            QRgb* out = reinterpret_cast<QRgb*>(blurred.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int r = 0, g = 0, b = 0, a = 0, count = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= h)
                        continue;
                    const QRgb* in = reinterpret_cast<const QRgb*>(img.constScanLine(yy));
                    for (int dx = -radius; dx <= radius; ++dx) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= w)
                            continue;
                        QRgb p = in[xx];
                        r += qRed(p);
                        g += qGreen(p);
                        b += qBlue(p);
                        a += qAlpha(p);
                        ++count;
                    }
                }
                out[x] = qRgba(r / count, g / count, b / count, a / count);
            }
        }
        img = blurred;
        break;
    }
    case FilterType::Sharpen: {
        QImage base = source.convertToFormat(QImage::Format_ARGB32);
        img = base.copy();
        // 锐化结果与原图按强度混合：k=0 无效果，k=1（强度 50）全锐化，
        // k=2（强度 100）过冲一倍 —— 卷积核不动，用混合系数调强度。
        for (int y = 1; y < h - 1; ++y) {
            QRgb* out = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 1; x < w - 1; ++x) {
                auto pixel = [&base, w, h](int xx, int yy) -> QRgb {
                    xx = qBound(0, xx, w - 1);
                    yy = qBound(0, yy, h - 1);
                    return reinterpret_cast<const QRgb*>(base.constScanLine(yy))[xx];
                };
                auto conv = [&pixel](const std::function<int(QRgb)>& channel, int cx, int cy) {
                    int v = 0;
                    v += -1 * channel(pixel(cx - 1, cy - 1));
                    v += -1 * channel(pixel(cx, cy - 1));
                    v += -1 * channel(pixel(cx + 1, cy - 1));
                    v += -1 * channel(pixel(cx - 1, cy));
                    v += 9 * channel(pixel(cx, cy));
                    v += -1 * channel(pixel(cx + 1, cy));
                    v += -1 * channel(pixel(cx - 1, cy + 1));
                    v += -1 * channel(pixel(cx, cy + 1));
                    v += -1 * channel(pixel(cx + 1, cy + 1));
                    return qBound(0, v, 255);
                };
                QRgb src = pixel(x, y);
                auto mix = [k](int orig, int sharp) {
                    return qBound(0, qRound(orig + (sharp - orig) * qMin(2.0, k)), 255);
                };
                int r = mix(qRed(src), conv([](QRgb c) { return qRed(c); }, x, y));
                int g = mix(qGreen(src), conv([](QRgb c) { return qGreen(c); }, x, y));
                int b = mix(qBlue(src), conv([](QRgb c) { return qBlue(c); }, x, y));
                out[x] = qRgba(r, g, b, qAlpha(src));
            }
        }
        break;
    }
    }

    return img;
}

} // namespace

EditEngine::EditEngine(QObject* parent) : QObject(parent) {}

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

QRectF EditEngine::cropBounds() const
{
    for (auto it = m_actions.rbegin(); it != m_actions.rend(); ++it) {
        if (it->toolType == EditToolType::Crop)
            return it->bounds;
    }
    return QRectF();
}

QImage EditEngine::render() const
{
    return renderWithSelection(QString());
}

QImage EditEngine::renderWithSelection(const QString& selectedId) const
{
    // 统一到 ARGB32 再处理：下游 applyMosaic 等滤镜用 QRgb*（每像素 4 字节）直接写
    // scanLine，而 loadImage 对 3 通道图返回的是 Format_RGB888（每像素 3 字节），
    // 不转换会造成堆越界写入。convertToFormat 同时保证底图非空时格式确定。
    QImage result;
    if (m_baseImage.isNull()) {
        result = QImage(800, 600, QImage::Format_ARGB32);
        result.fill(Qt::white);
    } else {
        result = m_baseImage.convertToFormat(QImage::Format_ARGB32);
    }

    QRectF crop = cropBounds();
    QRect cropRect;
    const bool hasCrop = !crop.isEmpty();
    if (hasCrop)
        cropRect = crop.toRect().intersected(result.rect());

    // Apply filter and mosaic actions to the base image before drawing annotations
    for (const auto& action : m_actions) {
        if (action.isFilter()) {
            // 区域滤镜：bounds 有实际尺寸时只对框选区域应用（拖拽框选功能 +
            // 大图上小区域的渲染加速）；点击（零尺寸 bounds）仍作用于全图。
            const QRectF bf = action.bounds;
            if (bf.width() > 2 && bf.height() > 2) {
                QRect region = bf.toRect().intersected(result.rect());
                if (!region.isEmpty()) {
                    QImage regionImg =
                        applyFilter(result.copy(region), action.filterType, action.filterStrength);
                    QPainter regionPainter(&result);
                    regionPainter.drawImage(region.topLeft(), regionImg);
                    regionPainter.end();
                    continue;
                }
            }
            result = applyFilter(result, action.filterType, action.filterStrength);
        } else if (action.toolType == EditToolType::Mosaic)
            result = applyMosaic(result, action);
    }

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const auto& action : m_actions) {
        if (action.isFilter() || action.toolType == EditToolType::Crop)
            continue;
        drawAction(&painter, action);
        if (action.id == selectedId && action.isMovable()) {
            QPen pen(Qt::white);
            pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(action.bounds.adjusted(-2, -2, 2, 2));
        }
    }
    painter.end();

    if (hasCrop && cropRect.isValid())
        result = result.copy(cropRect);

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
    case EditToolType::Pen: {
        if (action.points.size() > 1) {
            for (int i = 1; i < action.points.size(); ++i)
                painter->drawLine(action.points[i - 1], action.points[i]);
        }
        break;
    }
    case EditToolType::Mosaic: {
        // Mosaic effect is applied directly to the base image; draw only a subtle border.
        QPen mosaicPen(QColor(255, 255, 255, 120));
        mosaicPen.setWidth(1);
        mosaicPen.setStyle(Qt::DashLine);
        painter->setPen(mosaicPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(action.bounds);
        break;
    }
    case EditToolType::Text: {
        QFont font = painter->font();
        font.setFamily(action.fontFamily.isEmpty() ? QStringLiteral("Microsoft YaHei")
                                                   : action.fontFamily);
        font.setPointSize(action.fontSize > 0 ? action.fontSize : 16);
        font.setBold(action.fontBold);
        painter->setFont(font);
        painter->setPen(QPen(c));
        painter->setBrush(Qt::NoBrush);
        if (action.textBackground) {
            // 半透明底框：浅色底图上的白字/浅色字从此可读。
            QFontMetrics fm(font);
            const QRectF textRect =
                fm.boundingRect(action.bounds.toRect(),
                                Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, action.text);
            QColor bg(0, 0, 0, 110);
            painter->fillRect(textRect.adjusted(-4, -2, 4, 2), bg);
        }
        painter->drawText(action.bounds, Qt::AlignLeft | Qt::AlignTop, action.text);
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
    case EditToolType::Filter:
        // Filters are applied to the base image before annotations are drawn
        break;
    }
}

QRectF EditEngine::handleRect(const QRectF& bounds, int handleIndex)
{
    const qreal s = 6;
    switch (handleIndex) {
    case 0:
        return QRectF(bounds.left() - s, bounds.top() - s, s * 2, s * 2);
    case 1:
        return QRectF(bounds.center().x() - s, bounds.top() - s, s * 2, s * 2);
    case 2:
        return QRectF(bounds.right() - s, bounds.top() - s, s * 2, s * 2);
    case 3:
        return QRectF(bounds.left() - s, bounds.center().y() - s, s * 2, s * 2);
    case 4:
        return QRectF(bounds.right() - s, bounds.center().y() - s, s * 2, s * 2);
    case 5:
        return QRectF(bounds.left() - s, bounds.bottom() - s, s * 2, s * 2);
    case 6:
        return QRectF(bounds.center().x() - s, bounds.bottom() - s, s * 2, s * 2);
    case 7:
        return QRectF(bounds.right() - s, bounds.bottom() - s, s * 2, s * 2);
    default:
        break;
    }
    return QRectF();
}

int EditEngine::hitTest(const QRectF& bounds, const QPointF& pos, int handleSize)
{
    // 用 qreal 计算命中区间的半宽：整除会让奇数 handleSize 的命中范围偏小。
    const qreal half = handleSize / 2.0;
    for (int i = 0; i < 8; ++i) {
        if (handleRect(bounds, i).adjusted(-half, -half, half, half).contains(pos))
            return i;
    }
    if (bounds.contains(pos))
        return -1; // 整体移动
    return -2; // 未命中
}

} // namespace yingtu
