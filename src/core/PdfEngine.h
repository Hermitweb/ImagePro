#pragma once

#include <QObject>
#include <QSizeF>
#include <QString>
#include <QStringList>

namespace yingtu {

struct PdfSettings {
    enum PageSize { A4, A5, Letter, Custom };
    enum Layout { SinglePerPage, Grid2x2, Grid3x3, FitToPage };

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

    QString process(const QStringList& filePaths, bool* ok = nullptr);

    static QSizeF pageSizeMm(PdfSettings::PageSize size);

signals:
    void progress(int percent);
    void finished(const QString& outputPath);
    void error(const QString& message);

private:
    PdfSettings m_settings;
};

} // namespace yingtu
