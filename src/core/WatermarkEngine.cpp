#include "WatermarkEngine.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QFileInfo>

namespace yingtu {

WatermarkEngine::WatermarkEngine(QObject* parent)
    : QObject(parent)
{
}

QStringList WatermarkEngine::process(const QStringList& filePaths, bool* ok)
{
    if (ok) *ok = false;
    if (filePaths.isEmpty()) {
        emit error(tr("No images to watermark"));
        return QStringList();
    }

    QStringList outputPaths;
    int total = filePaths.size();
    for (int i = 0; i < total; ++i) {
        QString out = processSingle(filePaths.at(i));
        if (!out.isEmpty())
            outputPaths.append(out);
        emit progress(qRound((i + 1) * 100.0 / total));
    }

    if (ok) *ok = !outputPaths.isEmpty();
    emit finished(outputPaths);
    return outputPaths;
}

QString WatermarkEngine::processSingle(const QString& filePath)
{
    QImage img = ImageLoader::loadImage(filePath);
    if (img.isNull())
        return QString();

    QImage result = preview(img, m_settings);

    QFileInfo fi(filePath);
    QString fmt = m_settings.outputFormat == QStringLiteral("original") ? fi.suffix() : m_settings.outputFormat;
    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    QString outputPath;
    if (!m_settings.explicitOutputPath.isEmpty()) {
        outputPath = m_settings.explicitOutputPath;
    } else {
        QString dir = m_settings.explicitOutputDir;
        if (dir.isEmpty())
            dir = m_settings.outputDir.isEmpty() ? fi.absolutePath() : m_settings.outputDir;
        outputPath = FileUtils::generateUniqueOutputPath(dir, fi.completeBaseName() + QStringLiteral("_watermarked"),
                                                         QStringLiteral(".") + fmt.toLower());
    }

    int q = m_settings.quality;
    bool ok = ImageLoader::saveImage(result, outputPath, fmt, q);
    return ok ? outputPath : QString();
}

QImage WatermarkEngine::preview(const QImage& source, const WatermarkSettings& settings)
{
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);

    if (settings.type == WatermarkType::Text) {
        QFont font(settings.fontFamily, settings.fontSize);
        painter.setFont(font);
        QFontMetrics fm(font);
        QRect textRect = fm.boundingRect(settings.text);

        QColor c = settings.color;
        c.setAlpha(255 * settings.opacity / 100);
        painter.setPen(c);

        if (settings.tile) {
            for (int x = settings.margin; x < result.width() + textRect.width(); x += textRect.width() + settings.tileSpacing) {
                for (int y = settings.margin; y < result.height() + textRect.height();
                     y += textRect.height() + settings.tileSpacing) {
                    painter.save();
                    painter.translate(x, y);
                    painter.rotate(settings.rotation);
                    painter.drawText(-textRect.width() / 2, textRect.height() / 2, settings.text);
                    painter.restore();
                }
            }
        } else {
            int px = 0, py = 0;
            int rows = 3, cols = 3;
            int row = settings.position / cols;
            int col = settings.position % cols;
            switch (col) {
            case 0: px = settings.margin; break;
            case 1: px = (result.width() - textRect.width()) / 2; break;
            case 2: px = result.width() - textRect.width() - settings.margin; break;
            }
            switch (row) {
            case 0: py = settings.margin + textRect.height(); break;
            case 1: py = result.height() / 2 + textRect.height() / 2; break;
            case 2: py = result.height() - settings.margin; break;
            }

            painter.save();
            painter.translate(px, py);
            painter.rotate(settings.rotation);
            painter.drawText(0, 0, settings.text);
            painter.restore();
        }
    } else {
        QImage wm = ImageLoader::loadImage(settings.imagePath);
        if (!wm.isNull()) {
            wm = wm.scaled(result.width() / 4, result.height() / 4, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.setOpacity(settings.opacity / 100.0);
            painter.drawImage(result.width() - wm.width() - settings.margin,
                              result.height() - wm.height() - settings.margin, wm);
        }
    }

    painter.end();
    return result;
}

} // namespace yingtu
