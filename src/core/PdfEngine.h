#pragma once

#include <QObject>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <atomic>

namespace yingtu {

struct PdfSettings {
    enum PageSize {
        A4,
        A5,
        Letter,
        Custom
    };
    enum Layout {
        SinglePerPage,
        Grid2x2,
        Grid3x3,
        FitToPage
    };

    PageSize pageSize = A4;
    QSizeF customPageSize; // in mm
    Layout layout = SinglePerPage;
    qreal marginLeft = 20.0;
    qreal marginTop = 20.0;
    qreal marginRight = 20.0;
    qreal marginBottom = 20.0;
    int dpi = 150;
    QString outputPath;
};

class PdfEngine : public QObject
{
    Q_OBJECT
public:
    explicit PdfEngine(QObject* parent = nullptr);

    void setSettings(const PdfSettings& settings) { m_settings = settings; }
    PdfSettings settings() const { return m_settings; }

    /** 取消标志（可选）：置位后剩余文件跳过，已完成部分正常返回。 */
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; }

    QString process(const QStringList& filePaths, bool* ok = nullptr);

    static QSizeF pageSizeMm(PdfSettings::PageSize size);

signals:
    void progress(int percent);
    void finished(const QString& outputPath);
    void error(const QString& message);

private:
    PdfSettings m_settings;
    std::atomic<bool>* m_cancelFlag = nullptr;
    bool cancelRequested() const { return m_cancelFlag && m_cancelFlag->load(); }
};

} // namespace yingtu
