#include "PdfEngine.h"
#include "utils/ImageLoader.h"
#include <QFileInfo>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QtMath>

namespace yingtu {

PdfEngine::PdfEngine(QObject* parent)
    : QObject(parent)
{
}

QSizeF PdfEngine::pageSizeMm(PdfSettings::PageSize size)
{
    switch (size) {
    case PdfSettings::A4: return QSizeF(210.0, 297.0);
    case PdfSettings::A5: return QSizeF(148.0, 210.0);
    case PdfSettings::Letter: return QSizeF(215.9, 279.4);
    case PdfSettings::Custom: return QSizeF(210.0, 297.0);
    }
    return QSizeF(210.0, 297.0);
}

QString PdfEngine::process(const QStringList& filePaths, bool* ok)
{
    if (filePaths.isEmpty()) {
        if (ok) *ok = false;
        emit error(tr("No images to export."));
        return QString();
    }

    QString outputPath = m_settings.outputPath;
    if (outputPath.isEmpty()) {
        if (filePaths.size() == 1) {
            QFileInfo fi(filePaths.first());
            outputPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() + QStringLiteral(".pdf");
        } else {
            QFileInfo fi(filePaths.first());
            outputPath = fi.absolutePath() + QStringLiteral("/output.pdf");
        }
    }

    QPdfWriter writer(outputPath);
    writer.setResolution(m_settings.dpi);
    QSizeF pageMm = (m_settings.pageSize == PdfSettings::Custom && !m_settings.customPageSize.isEmpty())
                        ? m_settings.customPageSize
                        : pageSizeMm(m_settings.pageSize);
    writer.setPageSize(QPageSize(pageMm, QPageSize::Millimeter));

    QPainter painter(&writer);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal left = m_settings.marginLeft;
    const qreal top = m_settings.marginTop;
    const qreal right = m_settings.marginRight;
    const qreal bottom = m_settings.marginBottom;
    const qreal pageW = pageMm.width();
    const qreal pageH = pageMm.height();
    const qreal contentW = pageW - left - right;
    const qreal contentH = pageH - top - bottom;

    auto drawImage = [&](const QImage& img) {
        if (img.isNull()) return;
        QRectF target;
        if (m_settings.layout == PdfSettings::FitToPage) {
            QImage scaled = img.scaled(contentW * m_settings.dpi / 25.4,
                                       contentH * m_settings.dpi / 25.4,
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
            qreal x = left * m_settings.dpi / 25.4 + (contentW * m_settings.dpi / 25.4 - scaled.width()) / 2.0;
            qreal y = top * m_settings.dpi / 25.4 + (contentH * m_settings.dpi / 25.4 - scaled.height()) / 2.0;
            target = QRectF(x, y, scaled.width(), scaled.height());
            painter.drawImage(target, scaled);
        } else if (m_settings.layout == PdfSettings::SinglePerPage) {
            qreal sx = (contentW * m_settings.dpi / 25.4) / img.width();
            qreal sy = (contentH * m_settings.dpi / 25.4) / img.height();
            qreal scale = qMin(sx, sy);
            qreal w = img.width() * scale;
            qreal h = img.height() * scale;
            qreal x = left * m_settings.dpi / 25.4 + (contentW * m_settings.dpi / 25.4 - w) / 2.0;
            qreal y = top * m_settings.dpi / 25.4 + (contentH * m_settings.dpi / 25.4 - h) / 2.0;
            target = QRectF(x, y, w, h);
            painter.drawImage(target, img);
        } else {
            // Grid layouts: scale image to fit a single cell
            int cols = (m_settings.layout == PdfSettings::Grid3x3) ? 3 : 2;
            int rows = cols;
            qreal cellW = contentW / cols;
            qreal cellH = contentH / rows;
            qreal sx = (cellW * m_settings.dpi / 25.4) / img.width();
            qreal sy = (cellH * m_settings.dpi / 25.4) / img.height();
            qreal scale = qMin(sx, sy);
            qreal w = img.width() * scale;
            qreal h = img.height() * scale;
            // For grid layout, caller manages position; this lambda is used per image
            qreal x = left * m_settings.dpi / 25.4 + (cellW * m_settings.dpi / 25.4 - w) / 2.0;
            qreal y = top * m_settings.dpi / 25.4 + (cellH * m_settings.dpi / 25.4 - h) / 2.0;
            target = QRectF(x, y, w, h);
            painter.drawImage(target, img);
        }
    };

    if (m_settings.layout == PdfSettings::Grid2x2 || m_settings.layout == PdfSettings::Grid3x3) {
        int cols = (m_settings.layout == PdfSettings::Grid3x3) ? 3 : 2;
        int rows = cols;
        qreal cellW = contentW / cols;
        qreal cellH = contentH / rows;
        int perPage = cols * rows;

        for (int i = 0; i < filePaths.size(); ++i) {
            int slot = i % perPage;
            if (i > 0 && slot == 0)
                writer.newPage();

            int col = slot % cols;
            int row = slot / cols;
            qreal offsetX = left + col * cellW;
            qreal offsetY = top + row * cellH;

            QImage img = ImageLoader::loadImage(filePaths.at(i));
            if (img.isNull()) continue;

            qreal sx = (cellW * m_settings.dpi / 25.4) / img.width();
            qreal sy = (cellH * m_settings.dpi / 25.4) / img.height();
            qreal scale = qMin(sx, sy);
            qreal w = img.width() * scale;
            qreal h = img.height() * scale;
            qreal x = offsetX * m_settings.dpi / 25.4 + (cellW * m_settings.dpi / 25.4 - w) / 2.0;
            qreal y = offsetY * m_settings.dpi / 25.4 + (cellH * m_settings.dpi / 25.4 - h) / 2.0;
            painter.drawImage(QRectF(x, y, w, h), img);

            emit progress(qRound((i + 1) * 100.0 / filePaths.size()));
        }
    } else {
        for (int i = 0; i < filePaths.size(); ++i) {
            if (i > 0)
                writer.newPage();
            QImage img = ImageLoader::loadImage(filePaths.at(i));
            drawImage(img);
            emit progress(qRound((i + 1) * 100.0 / filePaths.size()));
        }
    }

    painter.end();

    if (ok) *ok = true;
    emit finished(outputPath);
    return outputPath;
}

} // namespace yingtu
