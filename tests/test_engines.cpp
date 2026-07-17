#include "core/CompressEngine.h"
#include "core/ConvertEngine.h"
#include "core/ResizeEngine.h"
#include "core/WatermarkEngine.h"
#include "utils/ImageLoader.h"
#include "utils/ResizeSettings.h"
#include <QTemporaryDir>
#include <QTest>
#include <QImage>

using namespace yingtu;

class TestEngines : public QObject
{
    Q_OBJECT

private slots:
    void testConvertEngine();
    void testCompressEngine();
    void testResizeEngine();
    void testWatermarkEngine();

private:
    QString createTestImage(const QString& path);
};

QString TestEngines::createTestImage(const QString& path)
{
    QImage image(200, 150, QImage::Format_RGB32);
    image.fill(Qt::green);
    if (!ImageLoader::saveImage(image, path))
        return QString();
    return path;
}

void TestEngines::testConvertEngine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString input = createTestImage(dir.path() + QStringLiteral("/input.png"));
    QVERIFY(!input.isEmpty());

    ConvertSettings settings;
    settings.targetFormat = QStringLiteral("jpg");
    settings.quality = 90;
    settings.outputDir = dir.path();

    ConvertEngine engine;
    engine.setSettings(settings);

    bool ok = false;
    QStringList outputs = engine.process(QStringList() << input, &ok);
    QVERIFY(ok);
    QCOMPARE(outputs.size(), 1);
    QVERIFY(QFile::exists(outputs.first()));
}

void TestEngines::testCompressEngine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString input = createTestImage(dir.path() + QStringLiteral("/input.png"));
    QVERIFY(!input.isEmpty());

    CompressSettings settings;
    settings.mode = CompressMode::Quality;
    settings.quality = 70;
    settings.outputDir = dir.path();

    CompressEngine engine;
    engine.setSettings(settings);

    QList<CompressResult> results = engine.process(QStringList() << input);
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().success);
    QVERIFY(QFile::exists(results.first().outputPath));
}

void TestEngines::testResizeEngine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString input = createTestImage(dir.path() + QStringLiteral("/input.png"));
    QVERIFY(!input.isEmpty());

    ResizeSettings settings;
    settings.mode = ResizeMode::Pixel;
    settings.targetWidth = 100;
    settings.targetHeight = 100;
    settings.lockAspectRatio = true;
    settings.outputDir = dir.path();

    ResizeEngine engine;
    engine.setSettings(settings);

    bool ok = false;
    QStringList outputs = engine.process(QStringList() << input, &ok);
    QVERIFY(ok);
    QCOMPARE(outputs.size(), 1);
    QVERIFY(QFile::exists(outputs.first()));

    QImage resized = ImageLoader::loadImage(outputs.first());
    QVERIFY(!resized.isNull());
}

void TestEngines::testWatermarkEngine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString input = createTestImage(dir.path() + QStringLiteral("/input.png"));
    QVERIFY(!input.isEmpty());

    WatermarkSettings settings;
    settings.type = WatermarkType::Text;
    settings.text = QStringLiteral("ImagePro");
    settings.fontSize = 24;
    settings.opacity = 80;
    settings.position = 4; // center
    settings.outputFormat = QStringLiteral("png");
    settings.outputDir = dir.path();

    WatermarkEngine engine;
    engine.setSettings(settings);

    bool ok = false;
    QStringList outputs = engine.process(QStringList() << input, &ok);
    QVERIFY(ok);
    QCOMPARE(outputs.size(), 1);
    QVERIFY(QFile::exists(outputs.first()));

    QImage watermarked = ImageLoader::loadImage(outputs.first());
    QVERIFY(!watermarked.isNull());
    QCOMPARE(watermarked.size(), QImage(input).size());
}

QTEST_MAIN(TestEngines)
#include "test_engines.moc"
