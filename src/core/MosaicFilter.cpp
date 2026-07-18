#include "MosaicFilter.h"

#include <QPainter>
#include <QRandomGenerator>
#include <QtMath>

namespace yingtu {

namespace {

QRgb averageColor(const QImage& src, const QRect& rect)
{
    if (src.isNull() || rect.isEmpty())
        return qRgba(0, 0, 0, 0);

    const int x0 = qMax(0, rect.left());
    const int y0 = qMax(0, rect.top());
    const int x1 = qMin(src.width() - 1, rect.right());
    const int y1 = qMin(src.height() - 1, rect.bottom());

    qint64 r = 0, g = 0, b = 0, a = 0;
    int count = 0;
    for (int y = y0; y <= y1; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = x0; x <= x1; ++x) {
            QRgb p = line[x];
            r += qRed(p); g += qGreen(p); b += qBlue(p); a += qAlpha(p);
            ++count;
        }
    }
    if (count == 0)
        return qRgba(0, 0, 0, 0);
    return qRgba(r / count, g / count, b / count, a / count);
}

QImage applySquareMosaic(const QImage& src, const QRect& rect, int blockSize)
{
    if (src.isNull() || rect.isEmpty() || blockSize < 2)
        return src;

    QImage result = src.copy();
    const int x0 = qMax(0, rect.left());
    const int y0 = qMax(0, rect.top());
    const int x1 = qMin(src.width(), rect.right() + 1);
    const int y1 = qMin(src.height(), rect.bottom() + 1);

    for (int by = y0; by < y1; by += blockSize) {
        for (int bx = x0; bx < x1; bx += blockSize) {
            QRect block(bx, by,
                        qMin(blockSize, x1 - bx),
                        qMin(blockSize, y1 - by));
            QRgb fill = averageColor(src, block);
            for (int y = block.top(); y <= block.bottom(); ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
                for (int x = block.left(); x <= block.right(); ++x)
                    line[x] = fill;
            }
        }
    }
    return result;
}

QImage applyHexagonMosaic(const QImage& src, const QRect& rect, int size)
{
    if (src.isNull() || rect.isEmpty() || size < 4)
        return src;

    QImage result = src.copy();
    QPainter painter(&result);
    painter.setClipRect(rect);
    painter.setPen(Qt::NoPen);

    const double h = size * qSqrt(3.0) / 2.0;
    const int x0 = qMax(0, rect.left() - size);
    const int y0 = qMax(0, rect.top() - int(h));
    const int x1 = qMin(src.width() - 1, rect.right() + size);
    const int y1 = qMin(src.height() - 1, rect.bottom() + int(h));

    int row = 0;
    for (double cy = y0; cy <= y1; cy += h, ++row) {
        double offset = (row % 2) * (size * 0.75);
        for (double cx = x0 + offset; cx <= x1; cx += size * 1.5) {
            QPolygonF hex;
            for (int i = 0; i < 6; ++i) {
                double angle = M_PI / 3.0 * i;
                hex << QPointF(cx + size * 0.5 * qCos(angle),
                               cy + size * 0.5 * qSin(angle));
            }
            QRect sampleRect(qMax(0, int(cx - size * 0.5)),
                             qMax(0, int(cy - h * 0.5)),
                             qMin(src.width() - int(cx - size * 0.5), size),
                             qMin(src.height() - int(cy - h * 0.5), int(h)));
            painter.setBrush(QColor(averageColor(src, sampleRect)));
            painter.drawPolygon(hex);
        }
    }
    painter.end();
    return result;
}

QImage applyCircleMosaic(const QImage& src, const QRect& rect, int size)
{
    if (src.isNull() || rect.isEmpty() || size < 4)
        return src;

    QImage result = src.copy();
    QPainter painter(&result);
    painter.setClipRect(rect);
    painter.setPen(Qt::NoPen);

    const int radius = size / 2;
    const int x0 = qMax(0, rect.left() - size);
    const int y0 = qMax(0, rect.top() - size);
    const int x1 = qMin(src.width() - 1, rect.right() + size);
    const int y1 = qMin(src.height() - 1, rect.bottom() + size);

    for (int cy = y0 + radius; cy <= y1; cy += size) {
        for (int cx = x0 + radius; cx <= x1; cx += size) {
            QRect sampleRect(qMax(0, cx - radius), qMax(0, cy - radius),
                             qMin(src.width() - cx + radius, size),
                             qMin(src.height() - cy + radius, size));
            painter.setBrush(QColor(averageColor(src, sampleRect)));
            painter.drawEllipse(QPointF(cx, cy), radius, radius);
        }
    }
    painter.end();
    return result;
}

QImage applyBlurMosaic(const QImage& src, const QRect& rect, int size)
{
    if (src.isNull() || rect.isEmpty() || size < 1)
        return src;

    const int x0 = qMax(0, rect.left());
    const int y0 = qMax(0, rect.top());
    const int x1 = qMin(src.width(), rect.right() + 1);
    const int y1 = qMin(src.height(), rect.bottom() + 1);
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w <= 0 || h <= 0)
        return src;

    QImage region = src.copy(x0, y0, w, h).convertToFormat(QImage::Format_ARGB32);
    QImage blurred(w, h, QImage::Format_ARGB32);

    const int r = qBound(1, size / 2, 20);
    for (int y = 0; y < h; ++y) {
        QRgb* out = reinterpret_cast<QRgb*>(blurred.scanLine(y));
        for (int x = 0; x < w; ++x) {
            qint64 rr = 0, gg = 0, bb = 0, aa = 0;
            int count = 0;
            for (int dy = -r; dy <= r; ++dy) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                const QRgb* in = reinterpret_cast<const QRgb*>(region.constScanLine(yy));
                for (int dx = -r; dx <= r; ++dx) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= w) continue;
                    QRgb p = in[xx];
                    rr += qRed(p); gg += qGreen(p); bb += qBlue(p); aa += qAlpha(p);
                    ++count;
                }
            }
            out[x] = qRgba(rr / count, gg / count, bb / count, aa / count);
        }
    }

    QImage result = src.copy();
    QPainter painter(&result);
    painter.drawImage(x0, y0, blurred);
    painter.end();
    return result;
}

QImage applyMezzotint(const QImage& src, const QRect& rect, int size)
{
    if (src.isNull() || rect.isEmpty() || size < 1)
        return src;

    const int x0 = qMax(0, rect.left());
    const int y0 = qMax(0, rect.top());
    const int x1 = qMin(src.width(), rect.right() + 1);
    const int y1 = qMin(src.height(), rect.bottom() + 1);

    QImage result = src.copy();
    QPainter painter(&result);
    painter.setClipRect(rect);

    // 先转为灰度铜版底
    QImage gray = src.copy(x0, y0, x1 - x0, y1 - y0).convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < gray.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(gray.scanLine(y));
        for (int x = 0; x < gray.width(); ++x) {
            QRgb p = line[x];
            int v = qRound(0.299 * qRed(p) + 0.587 * qGreen(p) + 0.114 * qBlue(p));
            line[x] = qRgba(v, v, v, qAlpha(p));
        }
    }
    painter.drawImage(x0, y0, gray);

    // 随机划痕与斑点
    QRandomGenerator* rng = QRandomGenerator::global();
    const int density = qMax(1, 5000 / size);
    for (int i = 0; i < density; ++i) {
        int sx = x0 + rng->bounded(x1 - x0);
        int sy = y0 + rng->bounded(y1 - y0);
        int len = rng->bounded(size * 2) + 2;
        int shade = rng->bounded(80) + 20;
        QColor c(rng->bounded(2) ? shade : 255 - shade,
                 rng->bounded(2) ? shade : 255 - shade,
                 rng->bounded(2) ? shade : 255 - shade,
                 120);
        painter.setPen(QPen(c, qMax(1, size / 6), Qt::SolidLine, Qt::RoundCap));
        double angle = rng->bounded(360) * M_PI / 180.0;
        painter.drawLine(QPointF(sx, sy),
                         QPointF(sx + len * qCos(angle), sy + len * qSin(angle)));
    }
    painter.end();
    return result;
}

QImage applyColorHalftone(const QImage& src, const QRect& rect, int size)
{
    if (src.isNull() || rect.isEmpty() || size < 4)
        return src;

    QImage result = src.copy();
    QPainter painter(&result);
    painter.setClipRect(rect);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const int x0 = qMax(0, rect.left() - size);
    const int y0 = qMax(0, rect.top() - size);
    const int x1 = qMin(src.width() - 1, rect.right() + size);
    const int y1 = qMin(src.height() - 1, rect.bottom() + size);

    // CMY 三色网点，分别偏移 0/120/240 度
    struct Channel { int angle; int r; int g; int b; };
    const Channel channels[] = {
        { 15, 0, 255, 255 },   // C
        { 75, 255, 0, 255 },   // M
        { 135, 255, 255, 0 }   // Y
    };

    for (const auto& ch : channels) {
        const double rad = ch.angle * M_PI / 180.0;
        const double dx = qCos(rad) * size;
        const double dy = qSin(rad) * size;

        for (double cy = y0; cy <= y1; cy += size) {
            for (double cx = x0; cx <= x1; cx += size) {
                QPointF center(cx + dx * 0.5, cy + dy * 0.5);
                int sx = qBound(0, qRound(center.x()), src.width() - 1);
                int sy = qBound(0, qRound(center.y()), src.height() - 1);
                QRgb p = reinterpret_cast<const QRgb*>(src.constScanLine(sy))[sx];

                int intensity = 0;
                if (ch.r == 0) intensity = 255 - qRed(p);
                else if (ch.g == 0) intensity = 255 - qGreen(p);
                else if (ch.b == 0) intensity = 255 - qBlue(p);

                double radius = (size * 0.45) * intensity / 255.0;
                QColor dot(ch.r, ch.g, ch.b, 180);
                painter.setBrush(dot);
                painter.drawEllipse(center, radius, radius);
            }
        }
    }
    painter.end();
    return result;
}

} // namespace

QImage applyMosaic(const QImage& source, const EditAction& action)
{
    if (source.isNull() || action.bounds.isEmpty())
        return source;

    QRect rect = action.bounds.toRect();
    int size = qBound(4, action.mosaicSize, 200);

    switch (action.mosaicStyle) {
    case MosaicStyle::Square:        return applySquareMosaic(source, rect, size);
    case MosaicStyle::Hexagon:       return applyHexagonMosaic(source, rect, size);
    case MosaicStyle::Circle:        return applyCircleMosaic(source, rect, size);
    case MosaicStyle::Blur:          return applyBlurMosaic(source, rect, size);
    case MosaicStyle::Mezzotint:     return applyMezzotint(source, rect, size);
    case MosaicStyle::ColorHalftone: return applyColorHalftone(source, rect, size);
    }
    return source;
}

} // namespace yingtu
