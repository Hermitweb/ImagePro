#include "utils/ImageLoader.h"
#include <QTemporaryDir>
#include <QTest>
#include <QImage>

using namespace yingtu;

class TestSave : public QObject
{
    Q_OBJECT

private slots:
    void testSaveImageRoundTrip();
    void testSaveImageWithQuality();
};

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

QTEST_MAIN(TestSave)
#include "test_save.moc"
