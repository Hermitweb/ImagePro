#include "core/StitchEngine.h"
#include "utils/ImageLoader.h"
#include <QPainter>
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
    void testStitchUniformWidthWithMixedSizes();
    void testStitchPreview();
    void testStitchPreviewMaxLongEdge();
    void testStitchAutoCropEdges();

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

void TestStitch::testStitchUniformWidthWithMixedSizes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 不同宽度的图片 + Vertical + uniformWidth：会进入「缩放统一宽度」分支，
    // 也就是 StitchEngine::loadAndPrepareImages 中引用计数出错的那条路径
    // （曾对输入图重复 g_object_unref，使缩放结果的管线引用到已释放的图像）。
    // 除尺寸外必须校验像素颜色：释放后使用不一定立刻崩溃，但常常读出错误内容，
    // 只断言 isNull() 是抓不到的。
    QImage a(100, 60, QImage::Format_RGB32);
    a.fill(Qt::red);
    QImage b(150, 40, QImage::Format_RGB32);
    b.fill(Qt::blue);

    const QString pathA = dir.path() + QStringLiteral("/a.png");
    const QString pathB = dir.path() + QStringLiteral("/b.png");
    QVERIFY(ImageLoader::saveImage(a, pathA));
    QVERIFY(ImageLoader::saveImage(b, pathB));

    StitchSettings settings;
    settings.direction = StitchSettings::Vertical;
    settings.uniformWidth = true;
    settings.spacing = 0;
    settings.outputDir = dir.path();
    settings.baseName = QStringLiteral("uniform");

    StitchEngine engine;
    engine.setSettings(settings);

    bool ok = false;
    QString output = engine.process(QStringList() << pathA << pathB, &ok);
    QVERIFY(ok);
    QVERIFY(QFile::exists(output));

    QImage stitched = ImageLoader::loadImage(output);
    QVERIFY(!stitched.isNull());

    // 统一到最大宽度 150，高度按比例缩放：60 -> 90，40 -> 40
    QCOMPARE(stitched.width(), 150);
    QCOMPARE(stitched.height(), 90 + 40);

    const QRgb topPixel = stitched.pixel(75, 20);
    QVERIFY2(qRed(topPixel) > 200 && qBlue(topPixel) < 60,
             "upper half should come from the red image");

    const QRgb bottomPixel = stitched.pixel(75, 100);
    QVERIFY2(qBlue(bottomPixel) > 200 && qRed(bottomPixel) < 60,
             "lower half should come from the blue image");
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

void TestStitch::testStitchPreviewMaxLongEdge()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 构造较大图片，验证 preview 的长边限制能避免加载完整分辨率
    QImage image(500, 250, QImage::Format_RGB32);
    image.fill(Qt::red);
    QString path = dir.path() + QStringLiteral("/a.png");
    QVERIFY(ImageLoader::saveImage(image, path));

    StitchSettings settings;
    settings.direction = StitchSettings::Horizontal;
    settings.spacing = 0;

    const int maxLongEdge = 200;
    QImage preview = StitchEngine::preview(QStringList() << path, settings, maxLongEdge);
    QVERIFY(!preview.isNull());
    QCOMPARE(qMax(preview.width(), preview.height()), maxLongEdge);
}

void TestStitch::testStitchAutoCropEdges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 构造带白边的图片，验证 autoCropEdges 在 libvips 路径下仍能裁剪
    QImage image(120, 100, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.fillRect(10, 10, 100, 80, Qt::red);
    painter.end();

    QString path = dir.path() + QStringLiteral("/a.png");
    QVERIFY(ImageLoader::saveImage(image, path));

    StitchSettings settings;
    settings.direction = StitchSettings::Horizontal;
    settings.spacing = 0;
    settings.autoCropEdges = true;
    settings.outputDir = dir.path();

    StitchEngine engine;
    engine.setSettings(settings);

    bool ok = false;
    QString output = engine.process(QStringList() << path, &ok);
    QVERIFY(ok);
    QVERIFY(QFile::exists(output));

    QImage stitched = ImageLoader::loadImage(output);
    QVERIFY(!stitched.isNull());
    QCOMPARE(stitched.width(), 100);
    QCOMPARE(stitched.height(), 80);
}

QTEST_MAIN(TestStitch)
#include "test_stitch.moc"
