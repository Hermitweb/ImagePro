#pragma once

#include "utils/ResizeSettings.h"
#include <QImage>
#include <QObject>
#include <QStringList>

namespace yingtu {

class ResizeEngine : public QObject
{
    Q_OBJECT
public:
    explicit ResizeEngine(QObject* parent = nullptr);

    void setSettings(const ResizeSettings& settings) { m_settings = settings; }

    QStringList process(const QStringList& filePaths, bool* ok = nullptr);
    static QImage resize(const QImage& source, const ResizeSettings& settings);
    static QSize outputSize(const QImage& source, const ResizeSettings& settings);
    static QSize outputSize(const QSize& sourceSize, const ResizeSettings& settings);

signals:
    void progress(int percent);
    void finished(const QStringList& outputPaths);
    void error(const QString& message);

private:
    QString processSingle(const QString& filePath);

    ResizeSettings m_settings;
};

} // namespace yingtu
