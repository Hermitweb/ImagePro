#include "core/ImageItem.h"
#include "core/ImageListModel.h"
#include "utils/ErrorHandler.h"
#include "utils/ImageProError.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace yingtu;

// 图片列表模型的加载失败汇总上报。
//
// 覆盖批量场景的核心诉求：一次拖入大量坏图时，**不能**逐张上报错误。
// 逐张上报会让日志和用户提示完全被失败项淹没（几百张坏图 = 几百条错误）。
// 正确行为是：每批加载结束后汇总成一条上报。
class TestModel : public QObject
{
    Q_OBJECT

private slots:
    void testBatchFailuresAreReportedOnce();
    void testThumbnailNeverDecodesOnPaintPath();
    void testRotationReusesDecodedThumbnail();
};

void TestModel::testBatchFailuresAreReportedOnce()
{
    ImageListModel model;

    // 5 个指向不存在文件的条目：全部会加载失败。
    QStringList missing;
    for (int i = 0; i < 5; ++i)
        missing << QStringLiteral("/nonexistent/imagepro_missing_%1.png").arg(i);
    model.addImages(missing);

    QSignalSpy spy(&ErrorHandler::instance(), &ErrorHandler::errorOccurred);

    // 等待本批加载结束。加载是异步的（QtConcurrent），用事件循环推进。
    // 超时兜底，避免因实现变更导致用例永久挂起。
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (spy.count() > 0)
            break;
    }

    QVERIFY2(spy.count() > 0, "load failures should be reported to the error pipeline");
    QVERIFY2(
        spy.count() == 1,
        qPrintable(
            QStringLiteral("expected exactly one aggregated report, got %1").arg(spy.count())));

    // 上报的错误码应为文件读取类，且详情里带上失败数量。
    const ImageProError err = spy.first().at(0).value<ImageProError>();
    QVERIFY2(err.code() == ErrorCode::FILE_CORRUPTED || err.code() == ErrorCode::FILE_NOT_FOUND,
             "batch load failure should be reported as a file-read error");
    const QString detail = spy.first().at(1).toString();
    QVERIFY2(detail.contains(QStringLiteral("5")),
             qPrintable(QStringLiteral("detail should carry the failure count: %1").arg(detail)));
}

void TestModel::testThumbnailNeverDecodesOnPaintPath()
{
    // thumbnail() 位于 data() → paintEvent 链路（UI 线程）。
    // 缓存未命中时它**绝不能**同步解码文件：那会让绘制路径卡住。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.path() + QStringLiteral("/big.png");
    QImage big(4000, 3000, QImage::Format_RGB32);
    big.fill(Qt::blue);
    QVERIFY(big.save(path));

    ImageItem item(path, false);
    ImageInfo info;
    info.valid = true;
    info.width = 4000;
    info.height = 3000;
    item.setInfo(info);

    QElapsedTimer timer;
    timer.start();
    const QPixmap thumb = item.thumbnail(240);
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY2(thumb.isNull(),
             "a cold cache must not produce a thumbnail by decoding on the paint path");
    QVERIFY2(
        elapsedMs < 50,
        qPrintable(QStringLiteral("thumbnail() took %1 ms on a cold cache - it must not decode")
                       .arg(elapsedMs)));
}

void TestModel::testRotationReusesDecodedThumbnail()
{
    // 旋转/翻转后必须能**复用**已有的解码结果，只重做几何变换。
    // 原先变换一改就把解码缓存一起丢掉，下一次绘制只能在 UI 线程
    // 重新解码整张大图 —— 这是列表操作时最明显的卡顿来源。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.path() + QStringLiteral("/rot.png");
    QImage img(1200, 900, QImage::Format_RGB32);
    img.fill(Qt::green);
    img.save(path);

    ImageItem item(path, false);
    ImageInfo info;
    info.valid = true;
    info.width = 1200;
    info.height = 900;
    item.setInfo(info);

    // 模拟异步加载完成：原始解码结果进入缓存。
    item.setDecodedThumbnail(240, img);
    const QPixmap before = item.thumbnail(240);
    QVERIFY(!before.isNull());
    const int beforeW = before.width();
    const int beforeH = before.height();

    item.rotate90();

    QElapsedTimer timer;
    timer.start();
    const QPixmap rotated = item.thumbnail(240);
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY2(!rotated.isNull(), "rotation must reuse the decoded thumbnail, not lose it");
    QVERIFY2(elapsedMs < 50, qPrintable(QStringLiteral("thumbnail() after rotation took %1 ms - "
                                                       "it must reuse the decoded image")
                                            .arg(elapsedMs)));

    // 旋转 90° 后缩略图的宽高应当互换。
    QCOMPARE(rotated.width(), beforeH);
    QCOMPARE(rotated.height(), beforeW);
}

QTEST_MAIN(TestModel)
#include "test_model.moc"
