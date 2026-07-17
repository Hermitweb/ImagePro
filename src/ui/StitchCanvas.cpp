#include "StitchCanvas.h"
#include "app/StitchPreviewState.h"
#include "core/ImageItem.h"
#include "core/ImageListModel.h"
#include "utils/ImageLoader.h"

#include <QAbstractAnimation>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QTransform>
#include <QToolTip>
#include <QUrl>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QtMath>

namespace yingtu {

namespace {

constexpr int kButtonSize = 32;
constexpr int kButtonSpacing = 4;
constexpr int kButtonBarHeight = kButtonSize + kButtonSpacing * 2;
constexpr int kButtonBarMargin = 8;

QSize rotatedSize(const QSize& size, int rotation)
{
    if (rotation == 90 || rotation == 270)
        return QSize(size.height(), size.width());
    return size;
}

QRect rotateRect(const QRect& rect, const QSize& origSize, int rotation)
{
    switch (rotation) {
    case 90:
        return QRect(origSize.height() - 1 - rect.bottom(), rect.left(), rect.height(), rect.width());
    case 180:
        return QRect(origSize.width() - 1 - rect.right(), origSize.height() - 1 - rect.bottom(), rect.width(), rect.height());
    case 270:
        return QRect(rect.top(), origSize.width() - 1 - rect.right(), rect.height(), rect.width());
    default:
        return rect;
    }
}

} // namespace

class StitchCanvas::SpotlightWindow : public QWidget
{
public:
    explicit SpotlightWindow(QWidget* parent = nullptr)
        : QWidget(parent, Qt::Tool)
    {
        setMinimumSize(200, 200);
        setMaximumSize(800, 800);
        resize(400, 400);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setImage(const QImage& image, const QString& name)
    {
        m_image = image;
        m_name = name;
        updateTitle();
        update();
    }

    void setCenter(const QPointF& pos)
    {
        m_center = pos;
        update();
    }

    void setZoom(double zoom)
    {
        m_zoom = zoom;
        updateTitle();
        update();
    }

    void setColorPickerActive(bool active)
    {
        if (m_colorPicker == active)
            return;
        m_colorPicker = active;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        QRect viewRect = rect().adjusted(0, 0, 0, -kInfoHeight);
        if (!m_image.isNull()) {
            double srcW = viewRect.width() / m_zoom;
            double srcH = viewRect.height() / m_zoom;
            double srcX = qBound(0.0, m_center.x() - srcW / 2.0, qMax(0.0, double(m_image.width()) - srcW));
            double srcY = qBound(0.0, m_center.y() - srcH / 2.0, qMax(0.0, double(m_image.height()) - srcH));
            painter.drawImage(viewRect, m_image, QRectF(srcX, srcY, srcW, srcH));
        } else {
            painter.fillRect(viewRect, palette().color(QPalette::Base));
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(viewRect, Qt::AlignCenter, StitchCanvas::tr("No image"));
        }

        QRect infoRect(0, height() - kInfoHeight, width(), kInfoHeight);
        painter.fillRect(infoRect, QColor(60, 60, 60));
        painter.setPen(Qt::white);

        int x = 0;
        int y = 0;
        QString rgbText;
        if (!m_image.isNull()) {
            x = qBound(0, qRound(m_center.x()), m_image.width() - 1);
            y = qBound(0, qRound(m_center.y()), m_image.height() - 1);
            if (m_colorPicker) {
                QColor c(m_image.pixel(x, y));
                rgbText = QStringLiteral("  RGB(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
            }
        }

        QString text = StitchCanvas::tr("x=%1, y=%2%3").arg(x).arg(y).arg(rgbText);
        painter.drawText(infoRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        static const double levels[] = {2.0, 4.0, 8.0, 16.0};
        int idx = 1;
        for (int i = 0; i < 4; ++i) {
            if (qFuzzyCompare(m_zoom, levels[i])) {
                idx = i;
                break;
            }
        }

        if (event->angleDelta().y() > 0)
            idx = qMin(idx + 1, 3);
        else
            idx = qMax(idx - 1, 0);

        setZoom(levels[idx]);
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    void updateTitle()
    {
        setWindowTitle(StitchCanvas::tr("%1 @%2x").arg(m_name).arg(m_zoom));
    }

    static constexpr int kInfoHeight = 24;

    QImage m_image;
    QString m_name;
    QPointF m_center;
    double m_zoom = 4.0;
    bool m_colorPicker = false;
};

StitchCanvas::StitchCanvas(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);

    m_tooltipTimer = new QTimer(this);
    m_tooltipTimer->setSingleShot(true);
    m_tooltipTimer->setInterval(200);
    connect(m_tooltipTimer, &QTimer::timeout, this, [this]() {
        if (m_hoveredIndex >= 0 && m_hoveredIndex < m_inputRects.size() && m_hoveredIndex != m_highlightedIndex) {
            QToolTip::showText(QCursor::pos(),
                               tr("输入图 %1：%2×%3")
                                   .arg(m_hoveredIndex + 1)
                                   .arg(m_inputRects[m_hoveredIndex].width())
                                   .arg(m_inputRects[m_hoveredIndex].height()),
                               this);
        }
    });

    m_buttonBarFade = new QVariantAnimation(this);
    m_buttonBarFade->setDuration(200);
    connect(m_buttonBarFade, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_buttonBarOpacity = value.toDouble();
        update();
    });
}

StitchCanvas::~StitchCanvas() = default;

void StitchCanvas::setImageListModel(ImageListModel* model)
{
    m_model = model;
}

void StitchCanvas::setSynthesizedImage(const QImage& image)
{
    closeSpotlight();
    m_originalImage = limitImageSize(image);
    m_originalSynthesizedSize = image.size();
    m_originalInputRects.clear();
    m_viewRotation = 0;
    m_fitToWindow = true;
    m_panOffset = QPoint();
    m_highlightedIndex = -1;
    m_hoveredIndex = -1;
    updateDisplayData();
    updateState();
    updateTransform();
}

void StitchCanvas::setInputRects(const QVector<QRect>& rects)
{
    m_originalInputRects = rects;
    updateDisplayData();
    if (m_highlightedIndex >= m_inputRects.size()) {
        m_highlightedIndex = -1;
        closeSpotlight();
        updateState();
    }
    if (m_hoveredIndex >= m_inputRects.size())
        m_hoveredIndex = -1;
    updateButtonBar();
    update();
}

void StitchCanvas::reset()
{
    closeSpotlight();
    m_image = QImage();
    m_originalImage = QImage();
    m_synthesizedSize = QSize();
    m_originalSynthesizedSize = QSize();
    m_imageScale = 1.0;
    m_inputRects.clear();
    m_originalInputRects.clear();
    m_viewRotation = 0;
    m_zoomFactor = 1.0;
    m_panOffset = QPoint();
    m_fitToWindow = true;
    m_highlightedIndex = -1;
    m_hoveredIndex = -1;
    m_draggingOver = false;
    updateState();
    updateTransform();
}

void StitchCanvas::setZoom(double factor)
{
    if (m_image.isNull())
        return;
    m_fitToWindow = false;
    m_zoomFactor = qBound(s_minZoom, factor, s_maxZoom);
    updateTransform();
}

void StitchCanvas::zoomIn()
{
    setZoom(m_zoomFactor * 1.2);
}

void StitchCanvas::zoomOut()
{
    setZoom(m_zoomFactor / 1.2);
}

void StitchCanvas::fitToWindow()
{
    if (m_image.isNull())
        return;
    m_fitToWindow = true;
    m_panOffset = QPoint();
    updateTransform();
}

void StitchCanvas::resetZoom()
{
    setZoom(1.0);
}

void StitchCanvas::rotateLeft()
{
    m_viewRotation = (m_viewRotation + 270) % 360;
    updateDisplayData();
    updateTransform();
}

void StitchCanvas::rotateRight()
{
    m_viewRotation = (m_viewRotation + 90) % 360;
    updateDisplayData();
    updateTransform();
}

void StitchCanvas::updateDisplayData()
{
    if (m_originalImage.isNull()) {
        m_image = QImage();
        m_synthesizedSize = QSize();
        m_imageScale = 1.0;
        m_inputRects.clear();
        return;
    }

    QTransform transform;
    transform.rotate(m_viewRotation);
    m_image = m_originalImage.transformed(transform, Qt::SmoothTransformation);
    m_synthesizedSize = rotatedSize(m_originalSynthesizedSize, m_viewRotation);
    m_imageScale = m_synthesizedSize.isEmpty() ? 1.0 : m_image.width() / double(m_synthesizedSize.width());

    m_inputRects.resize(m_originalInputRects.size());
    for (int i = 0; i < m_originalInputRects.size(); ++i)
        m_inputRects[i] = rotateRect(m_originalInputRects[i], m_originalSynthesizedSize, m_viewRotation);
}

void StitchCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    painter.fillRect(rect(), palette().color(QPalette::Base));

    if (m_image.isNull()) {
        painter.setPen(palette().color(QPalette::Text));
        QString text = m_draggingOver ? tr("释放以加入拼接") : tr("点击或拖入图片到合成图");
        painter.drawText(rect(), Qt::AlignCenter, text);

        if (m_draggingOver) {
            painter.setPen(QPen(QColor(33, 150, 243), 2, Qt::DashLine));
            painter.setBrush(QColor(33, 150, 243, 30));
            painter.drawRect(rect().adjusted(4, 4, -4, -4));
        }
        return;
    }

    if (m_draggingOver) {
        painter.setPen(QPen(QColor(33, 150, 243), 2));
        painter.setBrush(QColor(33, 150, 243, 30));
        painter.drawRect(m_imageRect.adjusted(-2, -2, 2, 2));
    }

    painter.drawImage(m_imageRect, m_image, QRectF(m_image.rect()));

    // Highlighted dimming overlay.
    if (m_highlightedIndex >= 0 && m_highlightedIndex < m_inputRects.size()) {
        QPainterPath fullPath;
        fullPath.addRect(m_imageRect);
        QPainterPath selectedPath;
        selectedPath.addRect(mapRectToWidget(m_inputRects[m_highlightedIndex]));
        QPainterPath overlay = fullPath.subtracted(selectedPath);

        painter.save();
        painter.setClipPath(overlay);
        painter.fillRect(m_imageRect, QColor(0, 0, 0, 77));
        painter.restore();
    }

    // Input image boundaries and indices.
    for (int i = 0; i < m_inputRects.size(); ++i) {
        QRectF r = mapRectToWidget(m_inputRects[i]);

        QPen pen;
        QColor labelBg;
        QColor labelText;
        if (m_highlightedIndex == i) {
            pen = QPen(QColor(255, 193, 7), 2.0);
            labelBg = QColor(255, 152, 0);
            labelText = Qt::white;
        } else if (m_hoveredIndex == i) {
            pen = QPen(QColor(33, 150, 243), 2.0);
            labelBg = QColor(33, 150, 243);
            labelText = Qt::white;
        } else {
            pen = QPen(QColor(255, 255, 255, 180), 1.0);
            labelBg = Qt::white;
            labelText = Qt::black;
        }

        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);

        QString text = QString::number(i + 1);
        QFont font = painter.font();
        font.setPointSize(m_hoveredIndex == i ? 12 : 10);
        font.setBold(true);
        painter.setFont(font);

        QFontMetrics fm(font);
        int labelW = fm.horizontalAdvance(text) + 8;
        int labelH = fm.height() + 4;
        QRectF labelRect(r.left(), r.top(), labelW, labelH);

        painter.fillRect(labelRect, labelBg);
        painter.setPen(QColor(80, 80, 80));
        painter.drawRect(labelRect);
        painter.setPen(labelText);
        painter.drawText(labelRect, Qt::AlignCenter, text);
    }

    drawButtonBar(painter);
}

void StitchCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_buttonBarOpacity > 0.0) {
        int action = buttonAt(event->pos());
        if (action >= 0) {
            executeButtonAction(action);
            event->accept();
            return;
        }
    }

    if (m_image.isNull())
        return;

    QPointF imagePos = widgetToImage(event->pos());
    int index = inputIndexAt(imagePos);

    if (index >= 0) {
        setHighlightedIndex(index);
        emit inputImageClicked(index);
        event->accept();
        return;
    }

    if (m_imageRect.contains(event->pos())) {
        setHighlightedIndex(-1);
        m_mayPan = true;
        m_panning = false;
        m_panStart = event->pos();
        m_panOffsetStart = m_panOffset;
        event->accept();
    }
}

void StitchCanvas::mouseMoveEvent(QMouseEvent* event)
{
    bool shift = event->modifiers() & Qt::ShiftModifier;
    if (shift != m_shiftPressed) {
        m_shiftPressed = shift;
        if (m_spotlight)
            m_spotlight->setColorPickerActive(m_shiftPressed);
    }

    if (m_panning) {
        m_panOffset = m_panOffsetStart + (event->pos() - m_panStart);
        updateTransform();
        return;
    }

    if (m_mayPan && (event->pos() - m_panStart).manhattanLength() > 4) {
        m_panning = true;
        m_panOffsetStart = m_panOffset;
        m_panStart = event->pos();
        updateCursor();
        return;
    }

    if (m_image.isNull()) {
        updateCursor();
        return;
    }

    QPointF imagePos = widgetToImage(event->pos());
    int index = inputIndexAt(imagePos);

    if (m_buttonBarOpacity > 0.0) {
        int btn = buttonAt(event->pos());
        if (btn != m_hoveredButtonIndex) {
            m_hoveredButtonIndex = btn;
            if (btn >= 0)
                QToolTip::showText(QCursor::pos(), buttonTooltip(btn), this);
            update();
        }
    }

    setHoveredIndex(index);

    if (m_spotlight && index >= 0)
        updateSpotlight(imagePos);

    updateCursor();
}

void StitchCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        m_mayPan = false;
        updateCursor();
    }
}

void StitchCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_image.isNull())
        return;

    QPointF imagePos = widgetToImage(event->pos());
    int index = inputIndexAt(imagePos);
    if (index >= 0) {
        setHighlightedIndex(index);
        emit inputImageDoubleClicked(index);
        openSpotlight(index, imagePos);
        event->accept();
    }
}

void StitchCanvas::wheelEvent(QWheelEvent* event)
{
    if (m_image.isNull())
        return;

    double delta = event->angleDelta().y() / 120.0;
    if (delta == 0.0)
        return;

    double oldZoom = m_zoomFactor;
    double newZoom = qBound(s_minZoom, oldZoom * (delta > 0 ? 1.15 : 1.0 / 1.15), s_maxZoom);

    QPointF imagePos = widgetToImage(event->position().toPoint());
    m_zoomFactor = newZoom;
    m_fitToWindow = false;

    QPointF newWidgetPos = imageToWidget(imagePos);
    m_panOffset += (event->position().toPoint() - newWidgetPos).toPoint();

    updateTransform();
    event->accept();
}

void StitchCanvas::resizeEvent(QResizeEvent*)
{
    updateTransform();
}

void StitchCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        m_draggingOver = true;
        update();
    }
}

void StitchCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        m_draggingOver = true;
        update();
    }
}

void StitchCanvas::dropEvent(QDropEvent* event)
{
    m_draggingOver = false;
    update();

    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls())
        return;

    QStringList paths;
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }

    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        emit imageDropped(paths);
    }
}

void StitchCanvas::leaveEvent(QEvent*)
{
    setHoveredIndex(-1);
    m_hoveredButtonIndex = -1;
    m_mayPan = false;
    m_panning = false;
    updateCursor();
}

void StitchCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_spotlight) {
            closeSpotlight();
        } else {
            setHighlightedIndex(-1);
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Shift) {
        m_shiftPressed = true;
        if (m_spotlight)
            m_spotlight->setColorPickerActive(true);
        event->accept();
        return;
    }

    if (m_image.isNull()) {
        QWidget::keyPressEvent(event);
        return;
    }

    int step = event->modifiers() & Qt::ShiftModifier ? 5 : 20;
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        event->accept();
        return;
    case Qt::Key_Minus:
        zoomOut();
        event->accept();
        return;
    case Qt::Key_0:
        fitToWindow();
        event->accept();
        return;
    case Qt::Key_1:
        resetZoom();
        event->accept();
        return;
    case Qt::Key_Left:
        m_panOffset.setX(m_panOffset.x() - step);
        updateTransform();
        event->accept();
        return;
    case Qt::Key_Right:
        m_panOffset.setX(m_panOffset.x() + step);
        updateTransform();
        event->accept();
        return;
    case Qt::Key_Up:
        m_panOffset.setY(m_panOffset.y() - step);
        updateTransform();
        event->accept();
        return;
    case Qt::Key_Down:
        m_panOffset.setY(m_panOffset.y() + step);
        updateTransform();
        event->accept();
        return;
    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

void StitchCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift) {
        m_shiftPressed = false;
        if (m_spotlight)
            m_spotlight->setColorPickerActive(false);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void StitchCanvas::updateTransform()
{
    if (m_image.isNull()) {
        m_imageRect = QRectF();
        update();
        return;
    }

    if (m_fitToWindow) {
        QRect viewport = rect();
        QSize available(qMax(1, viewport.width() - s_viewportMargin * 2),
                        qMax(1, viewport.height() - s_viewportMargin * 2));
        double fx = available.width() / double(m_image.width());
        double fy = available.height() / double(m_image.height());
        m_zoomFactor = qBound(s_minZoom, qMin(fx, fy), s_maxZoom);
    }

    m_imageRect = computeImageRect();
    ensurePanInBounds();
    layoutButtonBar();
    updateButtonBar();
    update();
}

QRectF StitchCanvas::computeImageRect() const
{
    QSizeF scaled(m_image.width() * m_zoomFactor, m_image.height() * m_zoomFactor);
    QRect viewport = rect();
    QPointF topLeft((viewport.width() - scaled.width()) / 2.0 + m_panOffset.x(),
                    (viewport.height() - scaled.height()) / 2.0 + m_panOffset.y());
    return QRectF(topLeft, scaled);
}

QPointF StitchCanvas::widgetToImage(const QPointF& pos) const
{
    QPointF local = pos - m_imageRect.topLeft();
    return QPointF(local.x() / m_zoomFactor / m_imageScale,
                   local.y() / m_zoomFactor / m_imageScale);
}

QPointF StitchCanvas::imageToWidget(const QPointF& pos) const
{
    return m_imageRect.topLeft() + QPointF(pos.x() * m_imageScale * m_zoomFactor,
                                           pos.y() * m_imageScale * m_zoomFactor);
}

QRectF StitchCanvas::mapRectToWidget(const QRect& rect) const
{
    QPointF tl = imageToWidget(rect.topLeft());
    QPointF br = imageToWidget(rect.bottomRight());
    return QRectF(tl, br).normalized();
}

int StitchCanvas::inputIndexAt(const QPointF& imagePos) const
{
    for (int i = 0; i < m_inputRects.size(); ++i) {
        if (m_inputRects[i].contains(qRound(imagePos.x()), qRound(imagePos.y())))
            return i;
    }
    return -1;
}

void StitchCanvas::setHighlightedIndex(int index)
{
    if (m_highlightedIndex == index)
        return;
    m_highlightedIndex = index;
    if (index < 0)
        closeSpotlight();
    updateState();
    updateButtonBar();
    update();
}

void StitchCanvas::setHoveredIndex(int index)
{
    if (m_hoveredIndex == index)
        return;
    m_hoveredIndex = index;

    m_tooltipTimer->stop();
    if (index >= 0 && index != m_highlightedIndex)
        m_tooltipTimer->start();
    else
        QToolTip::hideText();

    updateButtonBar();
    update();
}

void StitchCanvas::updateCursor()
{
    if (m_panning) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_hoveredButtonIndex >= 0) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (m_hoveredIndex >= 0) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (m_mayPan) {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    unsetCursor();
}

void StitchCanvas::ensurePanInBounds()
{
    QRect viewport = rect();
    const int margin = s_viewportMargin;
    bool changed = false;

    if (m_imageRect.width() <= viewport.width()) {
        if (m_panOffset.x() != 0) {
            m_panOffset.setX(0);
            changed = true;
        }
    } else {
        int maxPan = qRound((m_imageRect.width() - viewport.width()) / 2.0 + margin);
        if (m_panOffset.x() < -maxPan) {
            m_panOffset.setX(-maxPan);
            changed = true;
        } else if (m_panOffset.x() > maxPan) {
            m_panOffset.setX(maxPan);
            changed = true;
        }
    }

    if (m_imageRect.height() <= viewport.height()) {
        if (m_panOffset.y() != 0) {
            m_panOffset.setY(0);
            changed = true;
        }
    } else {
        int maxPan = qRound((m_imageRect.height() - viewport.height()) / 2.0 + margin);
        if (m_panOffset.y() < -maxPan) {
            m_panOffset.setY(-maxPan);
            changed = true;
        } else if (m_panOffset.y() > maxPan) {
            m_panOffset.setY(maxPan);
            changed = true;
        }
    }

    if (changed)
        m_imageRect = computeImageRect();
}

void StitchCanvas::updateButtonBar()
{
    bool shouldShow = !m_image.isNull() && (m_highlightedIndex >= 0 || m_hoveredIndex >= 0);
    if (shouldShow == m_buttonBarVisible && m_buttonBarFade->state() != QAbstractAnimation::Running)
        return;

    m_buttonBarVisible = shouldShow;
    m_buttonBarFade->stop();
    m_buttonBarFade->setStartValue(m_buttonBarOpacity);
    m_buttonBarFade->setEndValue(shouldShow ? 1.0 : 0.0);
    m_buttonBarFade->start();
}

void StitchCanvas::updateState()
{
    auto& state = StitchPreviewState::instance();
    if (m_image.isNull()) {
        state.transitionTo(StitchPreviewState::State::Idle);
    } else if (m_highlightedIndex >= 0) {
        state.transitionTo(StitchPreviewState::State::Highlighted, m_highlightedIndex);
    } else {
        state.transitionTo(StitchPreviewState::State::Ready);
    }
}

void StitchCanvas::layoutButtonBar()
{
    if (m_image.isNull()) {
        m_buttonBarRect = QRect();
        m_buttonRects.clear();
        return;
    }

    const QStringList labels = {QStringLiteral("⟲"), QStringLiteral("⟳"), QStringLiteral("↔"),
                                QStringLiteral("↕"), QStringLiteral("✕"), QStringLiteral("ⓘ")};
    const int count = labels.size();
    const int barW = count * kButtonSize + (count + 1) * kButtonSpacing;
    const int barH = kButtonBarHeight;

    int x = qRound(m_imageRect.center().x() - barW / 2.0);
    int y = qRound(m_imageRect.top() - barH - kButtonBarMargin);
    if (y < 4)
        y = 4;

    m_buttonBarRect = QRect(x, y, barW, barH);
    m_buttonRects.resize(count);
    for (int i = 0; i < count; ++i) {
        m_buttonRects[i] = QRect(x + kButtonSpacing + i * (kButtonSize + kButtonSpacing),
                                 y + kButtonSpacing, kButtonSize, kButtonSize);
    }
}

void StitchCanvas::drawButtonBar(QPainter& painter)
{
    if (m_buttonBarOpacity <= 0.0 || m_buttonRects.isEmpty())
        return;

    int index = m_highlightedIndex >= 0 ? m_highlightedIndex : m_hoveredIndex;
    if (index < 0 || index >= m_inputRects.size())
        return;

    painter.save();
    painter.setOpacity(m_buttonBarOpacity);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(m_buttonBarRect, 8, 8);

    const QStringList labels = {QStringLiteral("⟲"), QStringLiteral("⟳"), QStringLiteral("↔"),
                                QStringLiteral("↕"), QStringLiteral("✕"), QStringLiteral("ⓘ")};

    for (int i = 0; i < labels.size(); ++i) {
        const QRect& r = m_buttonRects.at(i);
        if (i == m_hoveredButtonIndex) {
            painter.setBrush(QColor(255, 255, 255, 50));
            painter.drawRoundedRect(r, 4, 4);
        }
        painter.setPen(Qt::white);
        painter.drawText(r, Qt::AlignCenter, labels.at(i));
    }

    painter.restore();
}

int StitchCanvas::buttonAt(const QPoint& pos) const
{
    if (!m_buttonBarRect.contains(pos))
        return -1;
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (m_buttonRects.at(i).contains(pos))
            return i;
    }
    return -1;
}

QString StitchCanvas::buttonTooltip(int action) const
{
    switch (action) {
    case 0:
        return tr("左旋");
    case 1:
        return tr("右旋");
    case 2:
        return tr("水平翻转");
    case 3:
        return tr("垂直翻转");
    case 4:
        return tr("移除");
    case 5:
        return tr("信息");
    default:
        return QString();
    }
}

void StitchCanvas::executeButtonAction(int action)
{
    int index = m_highlightedIndex >= 0 ? m_highlightedIndex : m_hoveredIndex;
    if (index < 0 || index >= m_inputRects.size())
        return;

    switch (action) {
    case 0:
        emit rotateInputImageRequested(index, true);
        break;
    case 1:
        emit rotateInputImageRequested(index, false);
        break;
    case 2:
        emit flipInputImageHorizontalRequested(index);
        break;
    case 3:
        emit flipInputImageVerticalRequested(index);
        break;
    case 4:
        emit removeInputImageRequested(index);
        break;
    case 5:
        emit inputImageInfoRequested(index);
        break;
    default:
        break;
    }
}

void StitchCanvas::openSpotlight(int index, const QPointF& imagePos)
{
    if (!m_spotlight) {
        m_spotlight = new SpotlightWindow(this);
        connect(m_spotlight, &QObject::destroyed, this, [this]() { m_spotlight = nullptr; });
    }

    QImage image = loadOriginalImage(index);
    QString name = tr("输入图 %1").arg(index + 1);
    if (m_model && index >= 0 && index < m_model->rowCount()) {
        const ImageItem* item = m_model->itemAt(index);
        if (item && !item->displayName().isEmpty())
            name = item->displayName();
    }

    m_spotlight->setImage(image, name);
    m_spotlight->setZoom(4.0);
    m_spotlight->setColorPickerActive(m_shiftPressed);
    updateSpotlight(imagePos);

    auto& state = StitchPreviewState::instance();
    state.transitionTo(StitchPreviewState::State::Spotlight, index);

    m_spotlight->show();
    m_spotlight->raise();
    m_spotlight->activateWindow();
}

void StitchCanvas::closeSpotlight()
{
    if (!m_spotlight)
        return;
    m_spotlight->close();
    updateState();
}

void StitchCanvas::updateSpotlight(const QPointF& imagePos)
{
    if (!m_spotlight)
        return;

    int index = m_highlightedIndex >= 0 ? m_highlightedIndex : inputIndexAt(imagePos);
    if (index < 0 || index >= m_inputRects.size())
        return;

    QRect r = m_inputRects[index];
    QPointF local(qBound(0.0, imagePos.x() - r.x(), double(r.width())),
                  qBound(0.0, imagePos.y() - r.y(), double(r.height())));
    m_spotlight->setCenter(local);
}

QImage StitchCanvas::loadOriginalImage(int index) const
{
    if (m_model && index >= 0 && index < m_model->rowCount()) {
        const ImageItem* item = m_model->itemAt(index);
        if (item) {
            QImage img = item->loadImage();
            if (!img.isNull())
                return img;
            if (!item->filePath().isEmpty())
                return ImageLoader::loadImage(item->filePath());
        }
    }

    if (index >= 0 && index < m_inputRects.size() && !m_image.isNull()) {
        QRect r = m_inputRects[index];
        QRect scaled(qRound(r.x() * m_imageScale), qRound(r.y() * m_imageScale),
                     qRound(r.width() * m_imageScale), qRound(r.height() * m_imageScale));
        return m_image.copy(scaled);
    }

    return QImage();
}

QImage StitchCanvas::limitImageSize(const QImage& image)
{
    if (image.isNull())
        return image;

    QImage result = image;
    constexpr int kMaxSide = 4096;
    if (qMax(result.width(), result.height()) > kMaxSide) {
        result = result.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    constexpr qint64 kMemoryThreshold = 256LL * 1024 * 1024;
    qint64 memory = qint64(result.width()) * result.height() * 4;
    if (memory > kMemoryThreshold) {
        result = result.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return result;
}

} // namespace yingtu
