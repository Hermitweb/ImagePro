#include "PreviewWidget.h"
#include "utils/ImageLoader.h"
#include <QAction>
#include <QApplication>
#include <QtMath>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

namespace yingtu {

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_imageLabel = new QLabel(tr("No image to preview"), this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet(QStringLiteral("color: #909399;"));
    m_imageLabel->setContextMenuPolicy(Qt::NoContextMenu);
    m_scrollArea->setWidget(m_imageLabel);

    layout->addWidget(m_scrollArea);
    setLayout(layout);
}

void PreviewWidget::setImage(const QImage& image)
{
    m_image = image;
    m_sourcePath.clear();
    m_sourceSize = QSize();
    m_imageLabel->setText(QString());
    m_fitToWindow = true;
    updatePixmap();
}

void PreviewWidget::setSourcePath(const QString& path, int rotation,
                                  bool flippedHorizontal, bool flippedVertical)
{
    m_sourcePath = path;
    m_sourceRotation = rotation;
    m_sourceFlippedH = flippedHorizontal;
    m_sourceFlippedV = flippedVertical;
    m_image = QImage();
    invalidateSourcePreviewCache();
    if (!path.isEmpty()) {
        ImageInfo info = ImageLoader::loadInfo(path);
        m_sourceSize = info.valid ? QSize(info.width, info.height) : QSize();
    } else {
        m_sourceSize = QSize();
    }
    m_imageLabel->setText(QString());
    m_fitToWindow = true;
    updatePixmap();
}

void PreviewWidget::clear()
{
    m_image = QImage();
    m_sourcePath.clear();
    m_sourceSize = QSize();
    m_sourceRotation = 0;
    m_sourceFlippedH = false;
    m_sourceFlippedV = false;
    m_rotation = 0;
    m_flippedH = false;
    m_flippedV = false;
    m_zoomFactor = 1.0;
    m_fitToWindow = true;
    invalidateSourcePreviewCache();
    m_imageLabel->setPixmap(QPixmap());
    m_imageLabel->setText(tr("No image to preview"));
    m_imageLabel->resize(m_scrollArea->viewport()->size());
}

void PreviewWidget::setZoom(double factor)
{
    if (m_image.isNull() && m_sourcePath.isEmpty())
        return;
    m_fitToWindow = false;
    m_zoomFactor = qBound(0.1, factor, 10.0);
    updatePixmap();
    emit zoomChanged(m_zoomFactor);
}

void PreviewWidget::zoomIn()
{
    setZoom(m_zoomFactor * 1.2);
}

void PreviewWidget::zoomOut()
{
    setZoom(m_zoomFactor / 1.2);
}

void PreviewWidget::fitToWindow()
{
    m_fitToWindow = true;
    updatePixmap();
}

void PreviewWidget::resetZoom()
{
    m_fitToWindow = false;
    m_zoomFactor = 1.0;
    updatePixmap();
}

void PreviewWidget::rotateLeft()
{
    m_rotation = (m_rotation + 270) % 360;
    updatePixmap();
}

void PreviewWidget::rotateRight()
{
    m_rotation = (m_rotation + 90) % 360;
    updatePixmap();
}

void PreviewWidget::flipHorizontal()
{
    m_flippedH = !m_flippedH;
    updatePixmap();
}

void PreviewWidget::flipVertical()
{
    m_flippedV = !m_flippedV;
    updatePixmap();
}

void PreviewWidget::resetTransform()
{
    m_rotation = 0;
    m_flippedH = false;
    m_flippedV = false;
    updatePixmap();
}

void PreviewWidget::invalidateSourcePreviewCache()
{
    m_cachedSourcePreview = QImage();
    m_cachedSourcePreviewSize = QSize();
}

void PreviewWidget::setComparisonMode(bool enabled)
{
    m_comparisonMode = enabled;
    updatePixmap();
}

void PreviewWidget::setOriginalImage(const QImage& image)
{
    m_originalImage = image;
    if (m_comparisonMode)
        updatePixmap();
}

QImage PreviewWidget::displayedImage()
{
    return transformedImage();
}

QSize PreviewWidget::viewportSize() const
{
    if (!m_scrollArea)
        return QSize();
    return m_scrollArea->viewport()->size();
}

void PreviewWidget::wheelEvent(QWheelEvent* event)
{
    if (m_image.isNull() && m_sourcePath.isEmpty())
        return;

    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0)
            zoomIn();
        else
            zoomOut();
        event->accept();
    }
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_fitToWindow)
        updatePixmap();
}

QSize PreviewWidget::neededSourceSize() const
{
    QSize viewport = m_scrollArea->viewport()->size();
    if (viewport.isEmpty())
        viewport = QSize(1280, 720);
    double factor = m_fitToWindow ? 2.0 : m_zoomFactor;
    QSize needed(qCeil(viewport.width() * factor), qCeil(viewport.height() * factor));
    if (!m_sourceSize.isEmpty())
        needed = needed.boundedTo(m_sourceSize);
    return needed;
}

QImage PreviewWidget::loadSourcePreview(const QSize& targetSize)
{
    if (m_sourcePath.isEmpty() || targetSize.isEmpty())
        return QImage();

    // 缓存命中条件：同一路径、同一源翻转/旋转、目标尺寸足够覆盖当前请求
    if (!m_cachedSourcePreview.isNull() &&
        m_cachedSourcePreviewSize.isValid() &&
        m_cachedSourcePreviewSize.width() >= targetSize.width() &&
        m_cachedSourcePreviewSize.height() >= targetSize.height()) {
        return m_cachedSourcePreview;
    }

    QImage img = ImageLoader::loadPreview(m_sourcePath, targetSize);
    if (img.isNull())
        return img;

    QTransform transform;
    if (m_sourceFlippedH || m_sourceFlippedV)
        transform.scale(m_sourceFlippedH ? -1 : 1, m_sourceFlippedV ? -1 : 1);
    if (m_sourceRotation != 0)
        transform.rotate(m_sourceRotation);
    if (!transform.isIdentity())
        img = img.transformed(transform, Qt::SmoothTransformation);

    m_cachedSourcePreview = img;
    m_cachedSourcePreviewSize = targetSize;
    return img;
}

QImage PreviewWidget::baseImage()
{
    if (!m_image.isNull())
        return m_image;
    if (!m_sourcePath.isEmpty())
        return loadSourcePreview(neededSourceSize());
    return QImage();
}

QImage PreviewWidget::transformedImage()
{
    QImage img = baseImage();
    if (img.isNull())
        return img;

    QTransform transform;
    if (m_flippedH || m_flippedV)
        transform.scale(m_flippedH ? -1 : 1, m_flippedV ? -1 : 1);
    if (m_rotation != 0)
        transform.rotate(m_rotation);
    if (!transform.isIdentity())
        img = img.transformed(transform, Qt::SmoothTransformation);
    return img;
}

QImage PreviewWidget::createComparisonImage(const QImage& original, const QImage& processed) const
{
    QSize viewSize = m_scrollArea->viewport()->size();
    if (viewSize.isEmpty())
        viewSize = QSize(800, 600);

    int halfW = viewSize.width() / 2 - 8;
    int h = viewSize.height() - 40;

    QImage orig = original.scaled(halfW, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage proc = processed.scaled(halfW, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    int targetH = qMax(orig.height(), proc.height());
    int compositeW = orig.width() + proc.width() + 24;
    int compositeH = targetH + 32;

    QImage composite(compositeW, compositeH, QImage::Format_ARGB32);
    composite.fill(palette().color(QPalette::Base));

    QPainter painter(&composite);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    int y1 = (targetH - orig.height()) / 2 + 24;
    int y2 = (targetH - proc.height()) / 2 + 24;
    painter.drawImage(8, y1, orig);
    painter.drawImage(16 + orig.width(), y2, proc);

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRect(0, 4, orig.width() + 8, 20), Qt::AlignCenter, tr("Original"));
    painter.drawText(QRect(12 + orig.width(), 4, proc.width() + 8, 20), Qt::AlignCenter, tr("Processed"));

    return composite;
}

void PreviewWidget::updatePixmap()
{
    if (m_comparisonMode && !m_originalImage.isNull() && !m_image.isNull()) {
        QImage composite = createComparisonImage(m_originalImage, m_image);
        m_imageLabel->setPixmap(QPixmap::fromImage(composite));
        m_imageLabel->resize(composite.size());
        emit zoomChanged(1.0);
        return;
    }

    QImage img = transformedImage();
    if (img.isNull())
        return;

    double factor = m_zoomFactor;
    if (m_fitToWindow) {
        QSize viewSize = m_scrollArea->viewport()->size();
        factor = qMin(viewSize.width() / double(img.width()),
                      viewSize.height() / double(img.height()));
        factor = qBound(0.05, factor, 10.0);
        m_zoomFactor = factor;
    }

    QImage scaled = img.scaled(qRound(img.width() * factor),
                               qRound(img.height() * factor),
                               Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    m_imageLabel->setPixmap(QPixmap::fromImage(scaled));
    m_imageLabel->resize(scaled.size());
    emit zoomChanged(m_zoomFactor);
}

void PreviewWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);

    menu.addAction(tr("Original Size"), this, &PreviewWidget::resetZoom, QKeySequence(QStringLiteral("Ctrl+0")));
    menu.addAction(tr("Fit to Window"), this, &PreviewWidget::fitToWindow, QKeySequence(QStringLiteral("Ctrl+F")));
    menu.addSeparator();
    menu.addAction(tr("Rotate Left"), this, &PreviewWidget::rotateLeft);
    menu.addAction(tr("Rotate Right"), this, &PreviewWidget::rotateRight);
    menu.addAction(tr("Flip Horizontal"), this, &PreviewWidget::flipHorizontal);
    menu.addAction(tr("Flip Vertical"), this, &PreviewWidget::flipVertical);
    menu.addSeparator();
    menu.addAction(tr("Delete Current Image"), this, [this]() {
        if (!m_image.isNull() || !m_sourcePath.isEmpty())
            emit deleteCurrentRequested();
    });
    menu.addAction(tr("Rotate Current Left"), this, [this]() {
        if (!m_image.isNull() || !m_sourcePath.isEmpty())
            emit rotateCurrentRequested();
    });
    menu.addAction(tr("Rotate Current Right"), this, [this]() {
        if (!m_image.isNull() || !m_sourcePath.isEmpty())
            emit rotateCurrentRightRequested();
    });
    menu.addSeparator();
    menu.addAction(tr("Copy Image"), this, [this]() {
        QImage img = displayedImage();
        if (!img.isNull())
            QApplication::clipboard()->setImage(img);
    }, QKeySequence::Copy);
    menu.addAction(tr("Save Image"), this, [this]() {
        QImage img = displayedImage();
        if (img.isNull())
            return;
        QString path = QFileDialog::getSaveFileName(this, tr("Save Image"), QString(),
                                                    QStringLiteral("PNG (*.png);;JPEG (*.jpg);;All Files (*)"));
        if (!path.isEmpty())
            ImageLoader::saveImage(img, path);
    }, QKeySequence::Save);

    menu.exec(event->globalPos());
}

} // namespace yingtu
