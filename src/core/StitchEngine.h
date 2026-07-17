#pragma once

#include "utils/PresetData.h"
#include <QImage>
#include <QObject>
#include <QStringList>

namespace yingtu {

struct StitchSettings {
    enum Direction { Vertical, Horizontal, Grid };

    Direction direction = Vertical;
    int spacing = 0;
    QString background = QStringLiteral("transparent");
    QColor bgColor = Qt::white;
    bool uniformWidth = false;
    bool uniformHeight = false;
    bool removeWhiteEdges = false;
    bool autoCropEdges = false;
    int gridRows = 1;
    int gridColumns = 1;
    QString outputFormat = QStringLiteral("png");
    int quality = 90;
    QString outputDir;
    QString baseName = QStringLiteral("stitched");
    QString explicitOutputPath; // 若非空，直接保存到该路径（用户手动选择）
};

class StitchEngine : public QObject
{
    Q_OBJECT
public:
    explicit StitchEngine(QObject* parent = nullptr);

    void setSettings(const StitchSettings& settings) { m_settings = settings; }
    StitchSettings settings() const { return m_settings; }

    // 执行拼接，返回输出文件路径
    QString process(const QStringList& filePaths, bool* ok = nullptr);

    static QImage preview(const QStringList& filePaths, const StitchSettings& settings);

signals:
    void progress(int percent);
    void finished(const QString& outputPath);
    void error(const QString& message);

private:
    StitchSettings m_settings;
};

} // namespace yingtu
