#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

namespace yingtu {

enum class WatermarkType {
    Text,
    Image
};

struct WatermarkSettings {
    WatermarkType type = WatermarkType::Text;
    QString text = QStringLiteral("影图 ImagePro");
    QString imagePath;
    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int fontSize = 24;
    QColor color = Qt::white;
    int opacity = 50;
    int rotation = 0;
    int position = 4; // center
    bool tile = false;
    int tileSpacing = 100;
    int margin = 20;
    QString outputFormat = QStringLiteral("original");
    int quality = 90;
    QString outputDir;
};

class WatermarkEngine : public QObject
{
    Q_OBJECT
public:
    explicit WatermarkEngine(QObject* parent = nullptr);

    void setSettings(const WatermarkSettings& settings) { m_settings = settings; }

    QStringList process(const QStringList& filePaths, bool* ok = nullptr);
    static QImage preview(const QImage& source, const WatermarkSettings& settings);

signals:
    void progress(int percent);
    void finished(const QStringList& outputPaths);
    void error(const QString& message);

private:
    QString processSingle(const QString& filePath);

    WatermarkSettings m_settings;
};

} // namespace yingtu
