#include "utils/AppPaths.h"
#include "core/ExportManager.h"
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include "utils/ImageProError.h"
#include "utils/PresetData.h"
#include "utils/StitchPreset.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace yingtu;

// 工具层测试：文件路径工具、错误上报管线、预设校验规则。
//
// 错误上报管线此前完全不可达：ImageProError 没有注册为 Qt 元类型，
// 跨线程队列连接会在运行时被静默拒绝，错误码永远不会呈现给用户。

class TestUtils : public QObject
{
    Q_OBJECT

private slots:
    void testUniqueOutputPathCreatesNonExistingFile();
    void testUniqueOutputPathAppendsIndex();
    void testUniqueOutputPathFailsOnUnwritableDir();
    void testReserveUniqueOutputPathIsAtomic();
    void testReserveUniqueOutputPathAvoidsExistingFile();
    void testDiscardReservedPathRemovesOnlyEmptyFile();
    void testIsSupportedImage();
    void testSupportedImageFormats();
    void testFormatFileSize();
    void testErrorHandlerRecordsLastError();
    void testErrorHandlerEmitsSignal();
    void testErrorMetaTypeIsRegistered();
    void testErrorCrossThreadQueuedDelivery();
    void testPresetValidationRules();
    void testCopyToFolderOverwritesAtomically();
    void testCopyToFolderKeepsDestinationOnFailure();
    void testLoadInfoReportsMissingFile();
    void testLoadInfoQuietDoesNotReport();
};

void TestUtils::testUniqueOutputPathCreatesNonExistingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = FileUtils::generateUniqueOutputPath(dir.path(), QStringLiteral("out"),
                                                             QStringLiteral("png"));
    QVERIFY(!path.isEmpty());
    QVERIFY2(!QFile::exists(path), "generated path must not overwrite an existing file");
    QVERIFY(path.endsWith(QStringLiteral("out.png")));
    QVERIFY(path.startsWith(dir.path()));
}

void TestUtils::testUniqueOutputPathAppendsIndex()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 预先占用 base 名，逼出带序号的候选名。
    QFile first(dir.path() + QStringLiteral("/out.png"));
    QVERIFY(first.open(QIODevice::WriteOnly));
    first.write("x");
    first.close();

    const QString path = FileUtils::generateUniqueOutputPath(dir.path(), QStringLiteral("out"),
                                                             QStringLiteral("png"));
    QVERIFY(!path.isEmpty());
    QVERIFY(!QFile::exists(path));
    QVERIFY2(path.endsWith(QStringLiteral("out_1.png")), qPrintable(path));

    // 再占一个，序号应继续递增。
    QFile second(dir.path() + QStringLiteral("/out_1.png"));
    QVERIFY(second.open(QIODevice::WriteOnly));
    second.close();

    const QString path2 = FileUtils::generateUniqueOutputPath(dir.path(), QStringLiteral("out"),
                                                              QStringLiteral("png"));
    QVERIFY(path2.endsWith(QStringLiteral("out_2.png")));
}

void TestUtils::testUniqueOutputPathFailsOnUnwritableDir()
{
    // 目录创建失败时必须返回空串，由调用方报错；
    // 返回一个无效路径会让后续写入静默失败。
    //
    // 用「已存在的普通文件之下建子目录」来制造不可创建的位置：
    // 这在任何平台、任何权限配置下都必然失败（父级不是目录），
    // 比依赖某个盘符不存在更可靠。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString blocker = dir.path() + QStringLiteral("/blocker");
    QFile file(blocker);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a directory");
    file.close();

    const QString path = FileUtils::generateUniqueOutputPath(
        blocker + QStringLiteral("/sub"), QStringLiteral("out"), QStringLiteral("png"));
    QVERIFY2(path.isEmpty(), "unwritable directory should yield an empty path");
}

void TestUtils::testReserveUniqueOutputPathIsAtomic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 原子占位：返回的路径必须已经被创建（0 字节），
    // 这样「检查 + 占用」是原子的，两个并发任务不会拿到同一文件名。
    const QString first = FileUtils::reserveUniqueOutputPath(dir.path(), QStringLiteral("out"),
                                                             QStringLiteral("png"));
    QVERIFY(!first.isEmpty());
    QVERIFY(QFile::exists(first));
    QCOMPARE(QFileInfo(first).size(), 0);

    // 第二次预留必须拿到不同的路径。
    const QString second = FileUtils::reserveUniqueOutputPath(dir.path(), QStringLiteral("out"),
                                                              QStringLiteral("png"));
    QVERIFY(!second.isEmpty());
    QVERIFY2(first != second, "reserving twice must not hand out the same path");
    QVERIFY(QFile::exists(second));
}

void TestUtils::testReserveUniqueOutputPathAvoidsExistingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 预置一个同名文件：预留必须绕开它，绝不能覆盖用户已有文件。
    const QString existing = dir.path() + QStringLiteral("/out.png");
    QFile file(existing);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("user data");
    file.close();

    const QString reserved = FileUtils::reserveUniqueOutputPath(dir.path(), QStringLiteral("out"),
                                                                QStringLiteral("png"));
    QVERIFY(!reserved.isEmpty());
    QVERIFY2(reserved != existing, "must not reuse an existing file name");

    // 原有文件内容不得被改动。
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("user data"));
}

void TestUtils::testDiscardReservedPathRemovesOnlyEmptyFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 空占位文件应被清理。
    const QString empty = dir.path() + QStringLiteral("/empty.png");
    {
        QFile f(empty);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    FileUtils::discardReservedPath(empty);
    QVERIFY2(!QFile::exists(empty), "empty placeholder should be removed on failure");

    // 已有内容的产物不得被误删。
    const QString written = dir.path() + QStringLiteral("/written.png");
    {
        QFile f(written);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("content");
    }
    FileUtils::discardReservedPath(written);
    QVERIFY2(QFile::exists(written), "a written output must never be discarded");
}

void TestUtils::testIsSupportedImage()
{
    QVERIFY(FileUtils::isSupportedImage(QStringLiteral("/tmp/a.png")));
    QVERIFY(FileUtils::isSupportedImage(QStringLiteral("/tmp/a.JPG")));
    QVERIFY(FileUtils::isSupportedImage(QStringLiteral("/tmp/a.webp")));
    QVERIFY(!FileUtils::isSupportedImage(QStringLiteral("/tmp/a.txt")));
    QVERIFY(!FileUtils::isSupportedImage(QString()));
}

void TestUtils::testSupportedImageFormats()
{
    const QStringList formats = FileUtils::supportedImageFormats();
    QVERIFY(formats.contains(QStringLiteral("png")));
    QVERIFY(formats.contains(QStringLiteral("jpg")));
    QVERIFY(formats.contains(QStringLiteral("jpeg")));
    QVERIFY(formats.contains(QStringLiteral("webp")));
    QVERIFY(!formats.isEmpty());
}

void TestUtils::testFormatFileSize()
{
    QCOMPARE(FileUtils::formatFileSize(0), QStringLiteral("0 B"));
    const QString kb = FileUtils::formatFileSize(1024);
    QVERIFY2(kb.contains(QStringLiteral("KB")), qPrintable(kb));
    const QString mb = FileUtils::formatFileSize(1024 * 1024);
    QVERIFY2(mb.contains(QStringLiteral("MB")), qPrintable(mb));
}

void TestUtils::testErrorHandlerRecordsLastError()
{
    ErrorHandler& handler = ErrorHandler::instance();
    handler.report(ErrorCode::DISK_FULL, QStringLiteral("测试详情"));

    const ImageProError last = handler.lastError();
    QCOMPARE(static_cast<int>(last.code()), static_cast<int>(ErrorCode::DISK_FULL));
    QVERIFY(!last.message().isEmpty());
    QVERIFY(!last.codeString().isEmpty());
}

void TestUtils::testErrorHandlerEmitsSignal()
{
    ErrorHandler& handler = ErrorHandler::instance();
    QSignalSpy spy(&handler, &ErrorHandler::errorOccurred);

    handler.report(ErrorCode::PERMISSION_DENIED, QStringLiteral("权限不足"));
    QCOMPARE(spy.count(), 1);

    const QList<QVariant> args = spy.takeFirst();
    const ImageProError err = args.at(0).value<ImageProError>();
    QCOMPARE(static_cast<int>(err.code()), static_cast<int>(ErrorCode::PERMISSION_DENIED));
    QCOMPARE(args.at(1).toString(), QStringLiteral("权限不足"));
}

void TestUtils::testErrorMetaTypeIsRegistered()
{
    // M4 回归：ImageProError 必须在运行时注册为元类型，
    // 否则跨线程队列连接会在投递阶段被拒绝，错误被静默丢弃。
    const int id = qMetaTypeId<ImageProError>();
    QVERIFY2(id != QMetaType::UnknownType, "ImageProError must be a registered metatype");
}

void TestUtils::testErrorCrossThreadQueuedDelivery()
{
    // 端到端验证：队列连接 + 元类型注册都到位时，
    // 上报的错误必须能投递到接收者。
    // 队列连接要求 payload 类型已注册为元类型，否则投递会被静默拒绝——
    // 这正是 M4 的故障模式：编译通过、运行时错误被丢弃。
    // 用 lambda 作为接收端：不需要额外的 Q_OBJECT 子类。
    QObject receiver;
    int count = 0;
    ErrorCode lastCode = ErrorCode::UNKNOWN;

    ErrorHandler& handler = ErrorHandler::instance();
    // 显式指定 QueuedConnection：同线程下默认会自动退化为 DirectConnection，
    // 那就绕过了元类型投递路径，测不出 M4 的故障。
    connect(
        &handler, &ErrorHandler::errorOccurred, &receiver,
        [&count, &lastCode](const ImageProError& error, const QString& /*detail*/) {
            ++count;
            lastCode = error.code();
        },
        Qt::QueuedConnection);

    handler.report(ErrorCode::FILE_NOT_FOUND, QStringLiteral("跨线程"));

    QTRY_COMPARE_WITH_TIMEOUT(count, 1, 3000);
    QCOMPARE(static_cast<int>(lastCode), static_cast<int>(ErrorCode::FILE_NOT_FOUND));
}

void TestUtils::testPresetValidationRules()
{
    // 网格预设：rows/columns 必须为正，否则作为除数会除零或退化输出。
    GridPreset grid;
    grid.id = QStringLiteral("g");
    grid.name = QStringLiteral("g");
    grid.rows = 0;
    grid.columns = 2;
    QVERIFY(!grid.isValid());
    grid.rows = 2;
    grid.columns = -1;
    QVERIFY(!grid.isValid());
    grid.columns = 2;
    QVERIFY(grid.isValid());
    grid.id.clear();
    QVERIFY(!grid.isValid());

    // 拼接预设：同样规则。
    StitchPreset stitch;
    stitch.id = QStringLiteral("s");
    stitch.name = QStringLiteral("s");
    stitch.rows = 0;
    QVERIFY(!stitch.isValid());
    stitch.rows = 1;
    stitch.columns = 1;
    QVERIFY(stitch.isValid());

    // 配置目录可被环境变量覆盖（测试隔离与便携模式的基础）。
    const QString original = qEnvironmentVariable("IMAGEPRO_CONFIG_DIR");
    qputenv("IMAGEPRO_CONFIG_DIR", "/tmp/imagetpro-config");
    QCOMPARE(appConfigDir(), QStringLiteral("/tmp/imagetpro-config"));
    if (original.isEmpty())
        qunsetenv("IMAGEPRO_CONFIG_DIR");
    else
        qputenv("IMAGEPRO_CONFIG_DIR", original.toUtf8());
}

void TestUtils::testCopyToFolderOverwritesAtomically()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QStringLiteral("/src");
    const QString dstDir = dir.path() + QStringLiteral("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString src = srcDir + QStringLiteral("/a.txt");
    {
        QFile f(src);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("new content");
    }
    // 目标已存在同名文件：应当被覆盖。
    const QString dst = dstDir + QStringLiteral("/a.txt");
    {
        QFile f(dst);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("old content");
    }

    const QString copied = ExportManager::copyToFolder(src, dstDir);
    QCOMPARE(copied, dst);
    QVERIFY(QFile::exists(dst));

    QFile out(dst);
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), QByteArray("new content"));
}

void TestUtils::testCopyToFolderKeepsDestinationOnFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.path() + QStringLiteral("/src");
    const QString dstDir = dir.path() + QStringLiteral("/dst");
    QVERIFY(QDir().mkpath(srcDir));
    QVERIFY(QDir().mkpath(dstDir));

    const QString src = srcDir + QStringLiteral("/b.txt");
    {
        QFile f(src);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("replacement");
    }
    const QString dst = dstDir + QStringLiteral("/b.txt");
    {
        QFile f(dst);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("precious original");
    }

    // 占用目标文件：Windows 下未以 FILE_SHARE_DELETE 打开的文件无法被替换，
    // 复制必然在提交阶段失败。
    QFile locked(dst);
    QVERIFY(locked.open(QIODevice::ReadWrite));

    const QString copied = ExportManager::copyToFolder(src, dstDir);
    QVERIFY2(copied.isEmpty(), "copy must fail while the destination is locked");

    // 关键断言：失败不能毁掉原有文件。
    locked.close();
    QFile out(dst);
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), QByteArray("precious original"));
}

void TestUtils::testLoadInfoReportsMissingFile()
{
    // 普通读取路径必须上报：文件不存在是用户可见的错误（拖入的文件被移走等）。
    QSignalSpy spy(&ErrorHandler::instance(), &ErrorHandler::errorOccurred);

    const ImageInfo info =
        ImageLoader::loadInfo(QStringLiteral("/nonexistent/definitely_missing.png"));
    QVERIFY(!info.valid);
    QVERIFY(!info.errorString.isEmpty());
    QCOMPARE(spy.count(), 1);

    const ImageProError err = spy.takeFirst().at(0).value<ImageProError>();
    QCOMPARE(static_cast<int>(err.code()), static_cast<int>(ErrorCode::FILE_NOT_FOUND));
}

void TestUtils::testLoadInfoQuietDoesNotReport()
{
    // 批量路径必须不上报：一次拖入几百张坏图时，逐张上报会淹没日志与提示。
    // 由调用方（ImageListModel）汇总后一次性上报。
    QSignalSpy spy(&ErrorHandler::instance(), &ErrorHandler::errorOccurred);

    const ImageInfo info =
        ImageLoader::loadInfoQuiet(QStringLiteral("/nonexistent/definitely_missing.png"));
    QVERIFY(!info.valid);
    QVERIFY(!info.errorString.isEmpty());
    QVERIFY2(spy.count() == 0,
             "loadInfoQuiet must not report errors - batch loads would flood the log");
}

QTEST_MAIN(TestUtils)
#include "test_utils.moc"
