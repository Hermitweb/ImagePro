#include "utils/ImageLoader.h"
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QImage>

using namespace yingtu;

// 说明：本文件此前的断言只校验宽高，且只使用 Format_RGB32，因此完全无法发现
//   * RGB888/Grayscale8 保存时忽略 bytesPerLine（每行 4 字节填充）导致输出斜切；
//   * alpha 通道在加载时被 Format_RGB32 强制成 0xff（透明图变不透明）。
// 下面改用「逐像素比对 + 刻意选择非 4 倍数宽度」的方式，让这两类问题必然暴露。

class TestSave : public QObject
{
    Q_OBJECT

private slots:
    void testSaveImageRoundTrip();
    void testSaveImageWithQuality();
    void testRgb888RoundTripPreservesPixels();
    void testGrayscaleRoundTripPreservesPixels();
    void testAlphaPreserved();
    void testMissingFileReturnsNull();

private:
    // 返回首个不匹配的像素坐标（-1 表示全部一致），便于失败时定位。
    static QPoint firstMismatch(const QImage& expected, const QImage& actual);
};

QPoint TestSave::firstMismatch(const QImage& expected, const QImage& actual)
{
    if (expected.size() != actual.size())
        return QPoint(-1, -1);
    for (int y = 0; y < expected.height(); ++y) {
        for (int x = 0; x < expected.width(); ++x) {
            const QRgb a = expected.pixel(x, y);
            const QRgb b = actual.pixel(x, y);
            if (qRed(a) != qRed(b) || qGreen(a) != qGreen(b) || qBlue(a) != qBlue(b) ||
                qAlpha(a) != qAlpha(b)) {
                qWarning("pixel mismatch at (%d,%d): expected %08x got %08x", x, y, a, b);
                return QPoint(x, y);
            }
        }
    }
    return QPoint(-1, -1);
}

void TestSave::testSaveImageRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.path() + QStringLiteral("/test.png");
    QImage image(100, 80, QImage::Format_RGB32);
    image.fill(Qt::red);

    QVERIFY(ImageLoader::saveImage(image, path));
    QVERIFY(QFile::exists(path));

    QImage loaded = ImageLoader::loadImage(path);
    QCOMPARE(loaded.width(), 100);
    QCOMPARE(loaded.height(), 80);

    // 纯色图：不只看尺寸，也要确认颜色真的存下来了。
    const QRgb p = loaded.pixel(50, 40);
    QVERIFY2(qRed(p) > 200 && qGreen(p) < 60 && qBlue(p) < 60,
             "round-trip should preserve the solid red colour");
}

void TestSave::testSaveImageWithQuality()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.path() + QStringLiteral("/test.jpg");
    QImage image(100, 80, QImage::Format_RGB32);
    image.fill(Qt::blue);

    QVERIFY(ImageLoader::saveImage(image, path, QStringLiteral("jpg"), 85));
    QVERIFY(QFile::exists(path));
    QVERIFY(QFileInfo(path).size() > 0);
}

void TestSave::testRgb888RoundTripPreservesPixels()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/rgb888.png");

    // 宽度取 101：101*3 = 303 不是 4 的倍数，Qt 会在每行末尾填充 1 字节。
    // 若 qImageToVipsImage 直接把 constBits() 交给 libvips（按 width*3 行距读取），
    // 输出会逐行错位、整体斜切。
    const int w = 101;
    const int h = 40;
    QImage image(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        uchar* line = image.scanLine(y);
        for (int x = 0; x < w; ++x) {
            line[x * 3 + 0] = static_cast<uchar>((x * 7 + y * 3) % 256);
            line[x * 3 + 1] = static_cast<uchar>((x * 11 + y * 5) % 256);
            line[x * 3 + 2] = static_cast<uchar>((x * 13 + y * 17) % 256);
        }
    }

    QVERIFY(ImageLoader::saveImage(image, path, QStringLiteral("png")));
    QImage loaded = ImageLoader::loadImage(path);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.size(), QSize(w, h));

    const QPoint bad = firstMismatch(image, loaded);
    QVERIFY2(bad.x() < 0,
             "RGB888 round-trip changed pixel values - row padding (bytesPerLine) likely ignored");
}

void TestSave::testGrayscaleRoundTripPreservesPixels()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/gray.png");

    // 宽度 101 同样非 4 的倍数：Grayscale8 每行也会被填充。
    const int w = 101;
    const int h = 32;
    QImage image(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar* line = image.scanLine(y);
        for (int x = 0; x < w; ++x)
            line[x] = static_cast<uchar>((x * 3 + y * 5) % 256);
    }

    QVERIFY(ImageLoader::saveImage(image, path, QStringLiteral("png")));
    QImage loaded = ImageLoader::loadImage(path);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.size(), QSize(w, h));

    const QPoint bad = firstMismatch(image, loaded);
    QVERIFY2(
        bad.x() < 0,
        "Grayscale8 round-trip changed pixel values - row padding (bytesPerLine) likely ignored");
}

void TestSave::testAlphaPreserved()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/alpha.png");

    // 宽度刻意非 4 的倍数，同时覆盖 alpha 与行填充两个问题。
    const int w = 37;
    const int h = 23;
    QImage image(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            line[x] =
                qRgba((x * 7) % 256, (y * 11) % 256, ((x + y) * 13) % 256, (x * 5 + y * 9) % 256);
        }
    }

    QVERIFY(ImageLoader::saveImage(image, path, QStringLiteral("png")));
    QImage loaded = ImageLoader::loadImage(path);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.size(), QSize(w, h));
    QVERIFY2(loaded.hasAlphaChannel(), "loaded PNG lost its alpha channel");

    const QPoint bad = firstMismatch(image, loaded);
    QVERIFY2(bad.x() < 0, "alpha/RGB not preserved through save+load (alpha forced to opaque?)");
}

void TestSave::testMissingFileReturnsNull()
{
    // 错误路径此前完全没有测试覆盖。
    const QImage loaded =
        ImageLoader::loadImage(QStringLiteral("/nonexistent/definitely_missing.png"));
    QVERIFY(loaded.isNull());

    const ImageInfo info =
        ImageLoader::loadInfo(QStringLiteral("/nonexistent/definitely_missing.png"));
    QVERIFY(!info.valid);
}

QTEST_MAIN(TestSave)
#include "test_save.moc"
