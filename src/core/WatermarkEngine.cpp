#include "WatermarkEngine.h"
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QFileInfo>

namespace yingtu {

WatermarkEngine::WatermarkEngine(QObject* parent) : QObject(parent) {}

QStringList WatermarkEngine::process(const QStringList& filePaths, bool* ok)
{
    if (ok)
        *ok = false;
    if (filePaths.isEmpty()) {
        ErrorHandler::instance().report(ErrorCode::EMPTY_FOLDER, tr("No images to watermark"));
        emit error(tr("No images to watermark"));
        return QStringList();
    }

    QStringList outputPaths;
    int total = filePaths.size();
    for (int i = 0; i < total; ++i) {
        const QString& path = filePaths.at(i);
        if (!QFileInfo::exists(path)) {
            ErrorHandler::instance().report(ErrorCode::FILE_NOT_FOUND, path);
            emit progress(qRound((i + 1) * 100.0 / total));
            continue;
        }

        QString out = processSingle(path);
        if (!out.isEmpty()) {
            outputPaths.append(out);
        } else {
            ErrorHandler::instance().report(ErrorCode::WRITE_FAILED, path);
        }
        emit progress(qRound((i + 1) * 100.0 / total));
    }

    if (ok)
        *ok = !outputPaths.isEmpty();
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
    QString fmt = m_settings.outputFormat == QStringLiteral("original") ? fi.suffix()
                                                                        : m_settings.outputFormat;
    if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
        fmt = QStringLiteral("jpeg");

    QString outputPath;
    // reserved 表示该路径由本函数原子占位（而非用户显式指定），
    // 写入失败时才可以安全地清理占位文件。
    bool reserved = false;
    if (!m_settings.explicitOutputPath.isEmpty()) {
        outputPath = m_settings.explicitOutputPath;
    } else {
        QString dir = m_settings.explicitOutputDir;
        if (dir.isEmpty())
            dir = m_settings.outputDir.isEmpty() ? fi.absolutePath() : m_settings.outputDir;
        outputPath = FileUtils::reserveUniqueOutputPath(
            dir, fi.completeBaseName() + QStringLiteral("_watermarked"),
            QStringLiteral(".") + fmt.toLower());
        reserved = !outputPath.isEmpty();
    }
    if (outputPath.isEmpty())
        return QString();

    int q = m_settings.quality;
    bool ok = ImageLoader::saveImage(result, outputPath, fmt, q);
    if (ok)
        return outputPath;
    // 写入失败：清理占位文件，避免留下 0 字节的「假成功」产物。
    if (reserved)
        FileUtils::discardReservedPath(outputPath);
    return QString();
}

QImage WatermarkEngine::preview(const QImage& source, const WatermarkSettings& settings)
{
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);

    if (settings.type == WatermarkType::Text) {
        // 空文字直接返回：既无内容可画，也能避免平铺循环退化成 O(宽×高) 次
        // 无意义的 save/translate/rotate/restore（看起来就是界面卡死）。
        if (settings.text.isEmpty()) {
            painter.end();
            return result;
        }

        QFont font(settings.fontFamily, settings.fontSize);
        painter.setFont(font);
        QFontMetrics fm(font);
        QRect textRect = fm.boundingRect(settings.text);

        QColor c = settings.color;
        c.setAlpha(255 * settings.opacity / 100);
        painter.setPen(c);

        if (settings.tile) {
            // 步进必须为正：否则 tileSpacing 为 0（或文字为空导致 textRect 宽高为 0）时
            // 步进为 0，循环永不结束。
            const int stepX = qMax(1, textRect.width() + settings.tileSpacing);
            const int stepY = qMax(1, textRect.height() + settings.tileSpacing);
            for (int x = settings.margin; x < result.width() + textRect.width(); x += stepX) {
                for (int y = settings.margin; y < result.height() + textRect.height(); y += stepY) {
                    painter.save();
                    painter.translate(x, y);
                    painter.rotate(settings.rotation);
                    painter.drawText(-textRect.width() / 2, textRect.height() / 2, settings.text);
                    painter.restore();
                }
            }
        } else {
            int px = 0, py = 0;
            // 位置枚举按 3 列 × 3 行九宫格展开：col 决定水平锚点，row 决定垂直锚点。
            constexpr int cols = 3;
            int row = settings.position / cols;
            int col = settings.position % cols;
            switch (col) {
            case 0:
                px = settings.margin;
                break;
            case 1:
                px = (result.width() - textRect.width()) / 2;
                break;
            case 2:
                px = result.width() - textRect.width() - settings.margin;
                break;
            default:
                px = settings.margin;
                break;
            }
            switch (row) {
            case 0:
                py = settings.margin + textRect.height();
                break;
            case 1:
                py = result.height() / 2 + textRect.height() / 2;
                break;
            case 2:
                py = result.height() - settings.margin;
                break;
            default:
                py = settings.margin + textRect.height();
                break;
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
            wm = wm.scaled(result.width() / 4, result.height() / 4, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
            painter.setOpacity(settings.opacity / 100.0);
            painter.drawImage(result.width() - wm.width() - settings.margin,
                              result.height() - wm.height() - settings.margin, wm);
        }
    }

    painter.end();
    return result;
}

} // namespace yingtu
