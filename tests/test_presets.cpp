#include "utils/PresetManager.h"
#include "utils/ResizePresetManager.h"
#include "utils/StitchPresetManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace yingtu;

// 预设子系统的回归保护。
//
// 该子系统此前存在四类缺陷，全部表现为「用户预设静默消失」，且没有任何报错：
//   M20 rows/columns 非正仍被判为有效 → 保存成功但读不回来；
//   M21 内置预设每次调用生成新 id → 按 id 定位失效、删除匹配不上；
//   M22 category 从不读写 → 用户预设类别重启后回退为默认值；
//   M23 无 schema 版本、JSON 损坏时静默覆盖 → 用户预设永久丢失。
//
// 测试通过 IMAGEPRO_CONFIG_DIR 把配置目录指向临时目录，
// 既隔离用户真实配置，也让用例可重复。

class TestPresets : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testGridPresetRoundTrip();
    void testGridPresetCategoryPersisted();
    void testInvalidGridPresetRejected();
    void testBuiltInIdsStable();
    void testCorruptFileIsBackedUp();
    void testLegacyBareArrayFormatSupported();
    void testDeletePreset();
    void testResizePresetRoundTrip();
    void testStitchPresetRoundTrip();
    void testStitchSingleRowPresetDirection();

private:
    QString presetFilePath(const QString& name) const;

    QTemporaryDir m_configDir;
};

void TestPresets::initTestCase()
{
    QVERIFY(m_configDir.isValid());
    // 必须在任何预设管理器被调用之前设置：路径是每次调用时动态解析的。
    qputenv("IMAGEPRO_CONFIG_DIR", m_configDir.path().toUtf8());
}

void TestPresets::cleanupTestCase()
{
    qunsetenv("IMAGEPRO_CONFIG_DIR");
}

QString TestPresets::presetFilePath(const QString& name) const
{
    return m_configDir.path() + QLatin1Char('/') + name;
}

void TestPresets::testGridPresetRoundTrip()
{
    GridPreset p;
    p.id = QStringLiteral("user-1");
    p.name = QStringLiteral("我的网格");
    p.rows = 3;
    p.columns = 4;
    p.cellWidth = 200;
    p.cellHeight = 200;
    p.lockAspectRatio = false;
    p.spacing = 12;
    p.background = QStringLiteral("custom");
    p.bgColor = QStringLiteral("#123456");
    p.tags << QStringLiteral("tag-a") << QStringLiteral("tag-b");

    QVERIFY(PresetManager::instance().savePreset(p));

    const QList<GridPreset> loaded = PresetManager::instance().loadPresets();
    const GridPreset* found = nullptr;
    for (const GridPreset& item : loaded) {
        if (item.id == p.id)
            found = &item;
    }
    QVERIFY2(found, "saved preset should be loadable");
    QCOMPARE(found->name, p.name);
    QCOMPARE(found->rows, p.rows);
    QCOMPARE(found->columns, p.columns);
    QCOMPARE(found->cellWidth, p.cellWidth);
    QCOMPARE(found->cellHeight, p.cellHeight);
    QCOMPARE(found->lockAspectRatio, p.lockAspectRatio);
    QCOMPARE(found->spacing, p.spacing);
    QCOMPARE(found->background, p.background);
    QCOMPARE(found->bgColor, p.bgColor);
    QCOMPARE(found->tags, p.tags);
}

void TestPresets::testGridPresetCategoryPersisted()
{
    // M22 回归：category 此前既不写也不读，用户预设的类别在重启后回退为 "Custom"。
    GridPreset p;
    p.id = QStringLiteral("user-category");
    p.name = QStringLiteral("带分类的预设");
    p.category = QStringLiteral("Social");
    p.rows = 2;
    p.columns = 2;

    QVERIFY(PresetManager::instance().savePreset(p));

    const QList<GridPreset> loaded = PresetManager::instance().loadPresets();
    for (const GridPreset& item : loaded) {
        if (item.id != p.id)
            continue;
        QCOMPARE(item.category, QStringLiteral("Social"));
        return;
    }
    QFAIL("preset with category was not found after reload");
}

void TestPresets::testInvalidGridPresetRejected()
{
    // M20 回归：rows/columns 非正时必须拒绝保存，否则「保存成功却读不回来」。
    GridPreset badRows;
    badRows.id = QStringLiteral("bad-rows");
    badRows.name = QStringLiteral("零行");
    badRows.rows = 0;
    badRows.columns = 2;
    QVERIFY(!PresetManager::instance().savePreset(badRows));

    GridPreset badCols;
    badCols.id = QStringLiteral("bad-cols");
    badCols.name = QStringLiteral("负列");
    badCols.rows = 2;
    badCols.columns = -1;
    QVERIFY(!PresetManager::instance().savePreset(badCols));

    // 无效项不得出现在加载结果里。
    for (const GridPreset& item : PresetManager::instance().loadPresets()) {
        QVERIFY2(item.id != badRows.id && item.id != badCols.id,
                 "invalid presets must not be persisted");
        QVERIFY(item.isValid());
    }
}

void TestPresets::testBuiltInIdsStable()
{
    // M21 回归：内置预设 id 必须跨调用稳定，否则按 id 定位/删除全部失效。
    const QList<GridPreset> first = PresetManager::instance().builtInPresets();
    const QList<GridPreset> second = PresetManager::instance().builtInPresets();
    QCOMPARE(first.size(), second.size());
    for (int i = 0; i < first.size(); ++i) {
        QCOMPARE(first.at(i).id, second.at(i).id);
        QVERIFY2(!first.at(i).id.isEmpty(), "built-in preset must have a stable id");
    }
}

void TestPresets::testCorruptFileIsBackedUp()
{
    // M23 回归：文件损坏时不能静默覆盖用户数据。
    const QString path = presetFilePath(QStringLiteral("grid_presets.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ this is not valid json ");
        f.close();
    }

    // 损坏文件不应导致加载崩溃，且仍应返回内置预设。
    const QList<GridPreset> loaded = PresetManager::instance().loadPresets();
    QVERIFY(!loaded.isEmpty());

    // 此时执行保存应先备份损坏文件。
    GridPreset p;
    p.id = QStringLiteral("after-corrupt");
    p.name = QStringLiteral("损坏之后");
    p.rows = 1;
    p.columns = 1;
    QVERIFY(PresetManager::instance().savePreset(p));

    const QString backup = path + QStringLiteral(".corrupt.bak");
    QVERIFY2(QFile::exists(backup),
             "corrupted preset file should be backed up before being overwritten");

    // 恢复干净状态，避免影响后续用例。
    QFile::remove(path);
    QFile::remove(backup);
}

void TestPresets::testLegacyBareArrayFormatSupported()
{
    // M23 回归：旧版本写出的裸数组格式必须仍能读取（向后兼容）。
    const QString path = presetFilePath(QStringLiteral("grid_presets.json"));

    QJsonObject item;
    item[QStringLiteral("id")] = QStringLiteral("legacy-1");
    item[QStringLiteral("name")] = QStringLiteral("旧格式");
    item[QStringLiteral("rows")] = 2;
    item[QStringLiteral("columns")] = 2;

    QJsonArray arr;
    arr.append(item);

    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.close();
    }

    bool found = false;
    for (const GridPreset& p : PresetManager::instance().loadPresets()) {
        if (p.id == QStringLiteral("legacy-1")) {
            found = true;
            QCOMPARE(p.name, QStringLiteral("旧格式"));
            QCOMPARE(p.rows, 2);
            QCOMPARE(p.columns, 2);
        }
    }
    QVERIFY2(found, "legacy bare-array preset file should still be readable");

    QFile::remove(path);
}

void TestPresets::testDeletePreset()
{
    GridPreset p;
    p.id = QStringLiteral("to-delete");
    p.name = QStringLiteral("待删除");
    p.rows = 2;
    p.columns = 2;
    QVERIFY(PresetManager::instance().savePreset(p));
    QVERIFY(PresetManager::instance().presetExists(p.name));

    QVERIFY(PresetManager::instance().deletePreset(p.id));
    QVERIFY(!PresetManager::instance().presetExists(p.name));
}

void TestPresets::testResizePresetRoundTrip()
{
    ResizePreset p;
    p.id = QStringLiteral("resize-1");
    p.name = QStringLiteral("缩放预设");
    p.width = 1920;
    p.height = 1080;
    p.category = QStringLiteral("Social");
    p.description = QStringLiteral("1080p");

    QVERIFY(ResizePresetManager::instance().savePreset(p));

    bool found = false;
    for (const ResizePreset& item : ResizePresetManager::instance().loadPresets()) {
        if (item.id != p.id)
            continue;
        found = true;
        QCOMPARE(item.name, p.name);
        QCOMPARE(item.width, p.width);
        QCOMPARE(item.height, p.height);
        QCOMPARE(item.category, p.category);
        QCOMPARE(item.description, p.description);
    }
    QVERIFY2(found, "resize preset should survive save/load round-trip");

    QVERIFY(ResizePresetManager::instance().deletePreset(p.id));
}

void TestPresets::testStitchPresetRoundTrip()
{
    StitchPreset p;
    p.id = QStringLiteral("stitch-1");
    p.name = QStringLiteral("拼接预设");
    p.rows = 3;
    p.columns = 3;
    p.category = QStringLiteral("Social");
    p.description = QStringLiteral("3x3");
    p.direction = 2;

    QVERIFY(StitchPresetManager::instance().savePreset(p));

    bool found = false;
    for (const StitchPreset& item : StitchPresetManager::instance().loadPresets()) {
        if (item.id != p.id)
            continue;
        found = true;
        QCOMPARE(item.name, p.name);
        QCOMPARE(item.rows, p.rows);
        QCOMPARE(item.columns, p.columns);
        QCOMPARE(item.category, p.category);
        QCOMPARE(item.direction, 2);
    }
    QVERIFY2(found, "stitch preset should survive save/load round-trip");

    QVERIFY(StitchPresetManager::instance().deletePreset(p.id));
}

void TestPresets::testStitchSingleRowPresetDirection()
{
    // 「1 x N」预设此前是 rows=1/columns=1 的 Grid，套用后只输出一格，
    // 与名称和描述（Single row）完全不符。修正后应为 Horizontal 方向。
    const QList<StitchPreset> builtins = StitchPresetManager::instance().builtInPresets();

    const StitchPreset* singleRow = nullptr;
    for (const StitchPreset& p : builtins) {
        if (p.name == QStringLiteral("1 x N"))
            singleRow = &p;
    }
    QVERIFY2(singleRow, "built-in '1 x N' preset should exist");
    QCOMPARE(singleRow->direction, 1); // Horizontal

    // Horizontal 预设忽略 rows/columns，其「需要多少张图」应按单行计算而非网格乘积。
    QVERIFY(singleRow->isValid());
}

QTEST_MAIN(TestPresets)
#include "test_presets.moc"
