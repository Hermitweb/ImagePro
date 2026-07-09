#include "core/StitchEngine.h"
#include "utils/ImageLoader.h"
#include <QTemporaryDir>
#include <QTest>
#include <QImage>

using namespace yingtu;

class TestStitch : public QObject
{
    Q_OBJECT

private slots:
    void testStitchVertical();
    void testStitchGrid();
    void testStitchPreview();

private:
    QString createTestImage(const QString& path, const QColor& color);
};

QString TestStitch::createTestImage(const QString& path, const QColor& color)
{
    QImage image(100, 80, QImage::Format_RGB32);
    image.fill(color);
    if (!ImageLoader::saveImage(image, path))
        return QString();
    return path;
}

void TestStitch::testStitchVertical()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList inputs;
    inputs << createTestImage(dir.path() + QStringLiteral("/a.png"), Qt::red);
    inputs << createTestImage(dir.path() + QStringLiteral("/b.png"), Qt::blue);
    QVERIFY(!inputs.contains(QString()));

    StitchSettings settings;
    settings.direction = StitchSettings::Vertical;
    settings.spacing = 10;
    settings.outputDir = dir.path();

    StitchEngine engine;
    engine.setSettings(settings);

    bool ok = false;
    QString output = engine.process(inputs, &ok);
    QVERIFY(ok);
    QVERIFY(QFile::exists(output));

    QImage stitched = ImageLoader::loadImage(output);
    QVERIFY(!stitched.isNull());
    QCOMPARE(stitched.width(), 100);
    QCOMPARE(stitched.height(), 80 * 2 + 10);
}

void TestStitch::testStitchGrid()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList inputs;
    inputs << createTestImage(dir.path() + QStringLiteral("/a.png"), Qt::red);
    inputs << createTestImage(dir.path() + QStringLiteral("/b.png"), Qt::blue);
    inputs << createTestImage(dir.path() + QStringLiteral("/c.png"), Qt::green);
    inputs << createTestImage(dir.path() + QStringLiteral("/d.png"), Qt::yellow);
    QVERIFY(!inputs.contains(QString()));

    StitchSettings settings;
    settings.direction = StitchSettings::Grid;
    settings.gridRows = 2;
    settings.gridColumns = 2;
    settings.spacing = 5;
    settings.outputDir = dir.path();
    settings.baseName = QStringLiteral("grid");

    StitchEngine engine;
    engine.setSettings(settings);

    bool ok = false;
    QString output = engine.process(inputs, &ok);
    QVERIFY(ok);
    QVERIFY(QFile::exists(output));

    QImage stitched = ImageLoader::loadImage(output);
    QVERIFY(!stitched.isNull());
}

void TestStitch::testStitchPreview()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList inputs;
    inputs << createTestImage(dir.path() + QStringLiteral("/a.png"), Qt::red);
    inputs << createTestImage(dir.path() + QStringLiteral("/b.png"), Qt::blue);
    QVERIFY(!inputs.contains(QString()));

    StitchSettings settings;
    settings.direction = StitchSettings::Horizontal;
    settings.spacing = 0;

    QImage preview = StitchEngine::preview(inputs, settings);
    QVERIFY(!preview.isNull());
}

QTEST_MAIN(TestStitch)
#include "test_stitch.moc"
