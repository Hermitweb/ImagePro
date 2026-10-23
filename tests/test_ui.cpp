#include "core/ImageListModel.h"
#include "ui/ImageListWidget.h"
#include "ui/MainWindow.h"
#include "ui/PropertyPanel.h"
#include "ui/StatusBarWidget.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTranslator>

using namespace yingtu;

// UI 冒烟测试：验证主窗口的关键布局不变量。
//
// 这些断言针对「控件被裁掉 / 面板尺寸失控 / 窗口起不来」这类问题 ——
// 它们在纯逻辑测试里看不出来，但会直接毁掉使用体验。

namespace {

QString makeTempImage(const QString& dir, const QString& name, int w, int h, const QColor& color)
{
    const QString path = dir + QLatin1Char('/') + name;
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    img.save(path);
    return path;
}

} // namespace

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void testMainWindowLayoutInvariants();
    void testPropertyPanelScrollsWhenShort();
    void testStatusBarShowsOutputSize();
    void testSnapshotIsRenderable();
    void testChineseTranslationLoads();
    void testEnglishTranslationLoads();
    void testTranslationsAreComplete();
    void testLiveLanguageSwitch();
    void testChineseUiDiffersFromEnglish();

private:
    /** 载入指定语言的翻译，收集窗口里所有可见文本。 */
    static QStringList collectVisibleTexts(const QString& language);
};

void TestUi::testMainWindowLayoutInvariants()
{
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    // 窗口是否真正「暴露」取决于运行环境：
    // CI runner 没有交互式桌面，窗口不会暴露。
    // 这里只等一下（不阻断测试）：
    // 后面断言的是布局不变量，与是否暴露无关。
    (void)QTest::qWaitForWindowExposed(&window, 2000);

    // 三栏结构：左列表 / 中预览 / 右属性面板。
    auto* splitter = window.findChild<QSplitter*>();
    QVERIFY2(splitter, "main window must contain a splitter");
    QCOMPARE(splitter->count(), 3);

    // 属性面板宽度被限制在 200~260（与 README 声明一致）。
    auto* propertyPanel = window.findChild<PropertyPanel*>();
    QVERIFY(propertyPanel);
    QCOMPARE(propertyPanel->minimumWidth(), 200);
    QCOMPARE(propertyPanel->maximumWidth(), 260);

    // 左列表面板同样有宽度上下限，且不会挤占中栏。
    auto* listWidget = window.findChild<ImageListWidget*>();
    QVERIFY(listWidget);
    QVERIFY(listWidget->minimumWidth() >= 180);
    QVERIFY(listWidget->maximumWidth() <= 280);

    // 窗口变宽时，增量应给中栏（预览），左右两侧保持受限宽度。
    const int before = splitter->sizes().value(1);
    window.resize(1600, 800);
    QCoreApplication::processEvents();
    const int after = splitter->sizes().value(1);
    QVERIFY2(after > before, "the centre preview pane must absorb the extra width");
    QVERIFY2(splitter->sizes().value(2) <= 260,
             "the property panel must not grow beyond its maximum width");

    // 列表缩略图必须铺满面板宽度。
    // 网格度量原先只在「窗口 resize」时重算，而分隔条分配面板宽度触发的是
    // **视口 resize**，于是缩略图停留在早期估算的尺寸（面板里大片留白）。
    const int viewportWidth = listWidget->findChild<QAbstractScrollArea*>()->viewport()->width();
    QVERIFY2(listWidget->cellSize().width() >= viewportWidth - 2,
             qPrintable(QStringLiteral("cell width %1 does not fill the viewport %2")
                            .arg(listWidget->cellSize().width())
                            .arg(viewportWidth)));
}

void TestUi::testPropertyPanelScrollsWhenShort()
{
    // 关键不变量：属性面板的设置区必须放在滚动容器里。
    // 否则窗口高度不够时（最小 600px），拼接/水印面板的控件会被裁掉且无法访问。
    MainWindow window;
    auto* propertyPanel = window.findChild<PropertyPanel*>();
    QVERIFY(propertyPanel);

    auto* scrollArea = propertyPanel->findChild<QScrollArea*>();
    QVERIFY2(scrollArea, "property panel settings area must live inside a QScrollArea");
    QVERIFY(scrollArea->widget() != nullptr);
    QCOMPARE(scrollArea->widgetResizable(), true);

    // 窗口被压得很矮时，设置区仍应可达（通过滚动），而不是被裁掉。
    window.resize(1280, 420);
    window.show();
    // 窗口是否真正「暴露」取决于运行环境：
    // CI runner 没有交互式桌面，窗口不会暴露。
    // 这里只等一下（不阻断测试）：
    // 后面断言的是布局不变量，与是否暴露无关。
    (void)QTest::qWaitForWindowExposed(&window, 2000);
    const int viewportHeight = scrollArea->viewport()->height();
    QVERIFY2(viewportHeight > 0, "scroll area viewport must have a usable height");
    // 面板内容高于视口时应当出现纵向滚动条。
    QVERIFY2(scrollArea->widget()->height() >= viewportHeight,
             "settings widget must not be shorter than the viewport");
}

void TestUi::testStatusBarShowsOutputSize()
{
    // 拼接模式下状态栏必须显示输出尺寸。
    // 此前 MainWindow::updateStatusBar 在拼接分支 setOutputSize 之后又无条件
    // 覆盖了一次（此时 size 为空 → 标签被隐藏），拼接输出尺寸永远显示不出来。
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    // 窗口是否真正「暴露」取决于运行环境：
    // CI runner 没有交互式桌面，窗口不会暴露。
    // 这里只等一下（不阻断测试）：
    // 后面断言的是布局不变量，与是否暴露无关。
    (void)QTest::qWaitForWindowExposed(&window, 2000);

    auto* statusBar = window.findChild<StatusBarWidget*>();
    QVERIFY(statusBar);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QStringList paths;
    paths << makeTempImage(dir.path(), QStringLiteral("a.png"), 800, 600, Qt::red);
    paths << makeTempImage(dir.path(), QStringLiteral("b.png"), 640, 480, Qt::green);

    // 默认工具即为拼接（ToolBarWidget 构造时发出 toolChanged(Stitch)）。
    window.imageModel()->addImages(paths);

    // 图片信息是异步加载的：必须等加载完成，尺寸才known。
    QSignalSpy loadSpy(window.imageModel(), &ImageListModel::thumbnailLoadFinished);
    QVERIFY2(loadSpy.wait(5000), "thumbnail loading should finish within 5s");
    QCoreApplication::processEvents();

    // 纵向拼接：最大宽 800，总高 600+480=1080。
    const QString text = statusBar->outputSizeText();
    QVERIFY2(text.contains(QStringLiteral("800")) && text.contains(QStringLiteral("1080")),
             qPrintable(QStringLiteral("status bar output size = '%1'").arg(text)));
}

void TestUi::testSnapshotIsRenderable()
{
    // 把窗口渲染成 PNG 落到构建目录，供人工目视检查布局。
    // 布局问题（控件被裁、间距失衡、面板错位）在纯断言里看不出来，
    // 必须靠截图确认。
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    // 窗口是否真正「暴露」取决于运行环境：
    // CI runner 没有交互式桌面，窗口不会暴露。
    // 这里只等一下（不阻断测试）：
    // 后面断言的是布局不变量，与是否暴露无关。
    (void)QTest::qWaitForWindowExposed(&window, 2000);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QStringList paths;
    paths << makeTempImage(dir.path(), QStringLiteral("a.png"), 1600, 1200, Qt::red);
    paths << makeTempImage(dir.path(), QStringLiteral("b.png"), 1200, 1600, Qt::green);
    paths << makeTempImage(dir.path(), QStringLiteral("c.png"), 1024, 768, Qt::blue);
    window.imageModel()->addImages(paths);

    QSignalSpy loadSpy(window.imageModel(), &ImageListModel::thumbnailLoadFinished);
    QVERIFY2(loadSpy.wait(5000), "thumbnail loading should finish within 5s");
    QCoreApplication::processEvents();

    const QPixmap shot = window.grab();
    QVERIFY2(!shot.isNull(), "the window must be renderable");
    QVERIFY2(
        shot.width() >= 1200 && shot.height() >= 700,
        qPrintable(QStringLiteral("snapshot size = %1x%2").arg(shot.width()).arg(shot.height())));

    const QString outPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ui-snapshot.png"));
    QVERIFY2(shot.save(outPath),
             qPrintable(QStringLiteral("cannot save snapshot to %1").arg(outPath)));
    qInfo("UI snapshot written to %s", qPrintable(outPath));
}

void TestUi::testChineseTranslationLoads()
{
    // 载入中文翻译后，界面文本必须真的是中文。
    // 这条用例同时守住两件事：翻译文件能加载、翻译内容确实被用上。
    QTranslator translator;
    const QString qm = QStringLiteral(IMAGEPRO_TRANSLATIONS_DIR "/ImagePro_zh_CN.qm");
    QVERIFY2(translator.load(qm), qPrintable(QStringLiteral("cannot load %1").arg(qm)));
    QVERIFY(QCoreApplication::installTranslator(&translator));

    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        (void)QTest::qWaitForWindowExposed(&window);

        QCOMPARE(window.windowTitle(), QStringLiteral("影图"));

        auto* statusBar = window.findChild<StatusBarWidget*>();
        QVERIFY(statusBar);

        const bool saved = window.grab().save(
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ui-zh.png")));
        QVERIFY(saved);
    }

    QCoreApplication::removeTranslator(&translator);
}

void TestUi::testEnglishTranslationLoads()
{
    QTranslator translator;
    const QString qm = QStringLiteral(IMAGEPRO_TRANSLATIONS_DIR "/ImagePro_en.qm");
    QVERIFY2(translator.load(qm), qPrintable(QStringLiteral("cannot load %1").arg(qm)));
    QVERIFY(QCoreApplication::installTranslator(&translator));

    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        (void)QTest::qWaitForWindowExposed(&window);

        QCOMPARE(window.windowTitle(), QStringLiteral("ImagePro"));

        const bool saved = window.grab().save(
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ui-en.png")));
        QVERIFY(saved);
    }

    QCoreApplication::removeTranslator(&translator);
}

void TestUi::testTranslationsAreComplete()
{
    // 翻译完整性门禁：每个 .ts 文件里的每条消息都必须有译文，
    // 且源串不得包含中文（源语言统一为英文，中文只出现在译文里）。
    //
    // 此前缺少这道门禁，于是出现过「英文界面下列表右键菜单全是中文」的情况
    // —— 源串本身写成了中文，翻译文件里自然没有对应的英文译文。
    const QStringList files = {QStringLiteral(IMAGEPRO_TRANSLATIONS_DIR "/ImagePro_zh_CN.ts"),
                               QStringLiteral(IMAGEPRO_TRANSLATIONS_DIR "/ImagePro_en.ts")};

    for (const QString& path : files) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                 qPrintable(QStringLiteral("cannot open %1").arg(path)));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();

        // 统计未译条目：<translation type="unfinished">
        const int unfinished = content.count(QStringLiteral("type=\"unfinished\""));
        QVERIFY2(
            unfinished == 0,
            qPrintable(QStringLiteral("%1 has %2 untranslated entries").arg(path).arg(unfinished)));

        // 源串含中文 → 说明有人把中文写成了源语言
        QRegularExpression sourceRe(QStringLiteral("<source>(.*?)</source>"),
                                    QRegularExpression::DotMatchesEverythingOption);
        QRegularExpression chineseRe(QStringLiteral("[\\x{4e00}-\\x{9fff}]"));
        QRegularExpressionMatchIterator it = sourceRe.globalMatch(content);
        int checked = 0;
        while (it.hasNext()) {
            const QString source = it.next().captured(1);
            ++checked;
            QVERIFY2(!chineseRe.match(source).hasMatch(),
                     qPrintable(QStringLiteral("%1: source must not contain Chinese: %2")
                                    .arg(path)
                                    .arg(source)));
        }
        QVERIFY2(checked > 300,
                 qPrintable(
                     QStringLiteral("%1: unexpectedly few messages (%1)").arg(path).arg(checked)));
    }
}

void TestUi::testLiveLanguageSwitch()
{
    // 免重启切换：装上中文翻译 → 界面变中文；换成英文翻译 → 界面变英文。
    // 期间用户已填的控件取值必须保留 —— 否则「切换语言」会顺手把参数清空。
    QTranslator zhTranslator;
    QVERIFY(zhTranslator.load(QStringLiteral(IMAGEPRO_TRANSLATIONS_DIR "/ImagePro_zh_CN.qm")));
    QVERIFY(QCoreApplication::installTranslator(&zhTranslator));

    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        (void)QTest::qWaitForWindowExposed(&window);

        QCOMPARE(window.windowTitle(), QStringLiteral("影图"));

        auto* panel = window.findChild<PropertyPanel*>();
        QVERIFY(panel);

        // 找一个可填值的控件（方向下拉框），改掉它的值。
        QList<QComboBox*> combos = panel->findChildren<QComboBox*>();
        QVERIFY(!combos.isEmpty());
        QComboBox* combo = combos.first();
        const int originalIndex = combo->currentIndex();
        const int targetIndex = (originalIndex + 1) % qMax(1, combo->count());
        combo->setCurrentIndex(targetIndex);
        QCoreApplication::processEvents();

        // 切到英文：Qt 在装/卸翻译器后会向所有顶层控件投递 LanguageChange。
        QTranslator enTranslator;
        QVERIFY(enTranslator.load(QStringLiteral(IMAGEPRO_TRANSLATIONS_DIR "/ImagePro_en.qm")));
        QVERIFY(QCoreApplication::installTranslator(&enTranslator));
        QCoreApplication::processEvents();

        QCOMPARE(window.windowTitle(), QStringLiteral("ImagePro"));

        // 切换后面板里的标签应当是英文。
        bool foundEnglishLabel = false;
        for (QLabel* label : panel->findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("Direction")) ||
                label->text().contains(QStringLiteral("Spacing"))) {
                foundEnglishLabel = true;
                break;
            }
        }
        QVERIFY2(foundEnglishLabel,
                 "property panel labels should be in English after switching language");

        // 用户改过的取值必须还在。
        QVERIFY2(combo->currentIndex() == targetIndex,
                 qPrintable(QStringLiteral("combo index changed from %1 to %2 after the switch")
                                .arg(targetIndex)
                                .arg(combo->currentIndex())));

        QCoreApplication::removeTranslator(&enTranslator);
    }

    QCoreApplication::removeTranslator(&zhTranslator);
}

void TestUi::testChineseUiDiffersFromEnglish()
{
    // 中文界面与英文界面必须**逐项**不同（少数合理相同的除外）。
    //
    // 这条门禁针对一类很难在评审中发现的回归：某些 `tr()` 的源串被写成
    // 运行时字符串后，lupdate 收录不到就把对应译文当成过时条目删掉，
    // 中文界面于是静默地退回英文（曾发生在工具栏的 8 个按钮标签上）。
    //
    // 比较方式：两次用同样的构造顺序建窗口，按控件顺序逐一比对文本。
    // 「应当被翻译」的判据：英文原文含字母，且中英完全相同 —— 说明这条没翻译。
    // 允许少量例外（单位、专有名词如 PDF/Lanczos、格式串），但不能成片。
    const QStringList zhTexts = collectVisibleTexts("ImagePro_zh_CN");
    const QStringList enTexts = collectVisibleTexts("ImagePro_en");
    QVERIFY(!zhTexts.isEmpty());
    QCOMPARE(zhTexts.size(), enTexts.size());

    QStringList untranslated;
    for (int i = 0; i < enTexts.size(); ++i) {
        const QString english = enTexts.at(i);
        if (english != zhTexts.at(i) && !english.isEmpty())
            continue;
        // 英文原文里有字母、却和中译文一字不差 → 这条没翻译。
        bool hasLetter = false;
        for (const QChar ch : english) {
            if (ch.isLetter()) {
                hasLetter = true;
                break;
            }
        }
        if (hasLetter && !english.isEmpty())
            untranslated.append(english);
    }

    // 允许的例外（单位、专有名词、格式串、品牌名等合理相同的文本）
    // 实测基线为 5 条：'x'、'#FFFFFF'、'x'、品牌名、'English'。
    // 留一点余量，但绝不能成片 —— 8 个按钮标签丢失就会越界。
    QVERIFY2(untranslated.size() <= 8,
             qPrintable(QStringLiteral("%1 strings stayed English in the Chinese UI: %2")
                            .arg(untranslated.size())
                            .arg(untranslated.join(QStringLiteral(" | ")))));
}

QStringList TestUi::collectVisibleTexts(const QString& language)
{
    QTranslator translator;
    const QString qm =
        QStringLiteral(IMAGEPRO_TRANSLATIONS_DIR "/") + language + QStringLiteral(".qm");
    if (!translator.load(qm))
        return QStringList();
    QCoreApplication::installTranslator(&translator);

    QStringList texts;
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        (void)QTest::qWaitForWindowExposed(&window);

        // 按固定的遍历顺序收集，保证两次运行（中文/英文）可以逐项对应。
        const auto labels = window.findChildren<QLabel*>();
        for (QLabel* label : labels)
            texts.append(label->text().trimmed());
        const auto buttons = window.findChildren<QToolButton*>();
        for (QToolButton* button : buttons)
            texts.append(button->text().trimmed());
        const auto pushes = window.findChildren<QPushButton*>();
        for (QPushButton* push : pushes)
            texts.append(push->text().trimmed());
        const auto actions = window.findChildren<QAction*>();
        for (QAction* action : actions)
            texts.append(action->text().trimmed());
    }
    QCoreApplication::removeTranslator(&translator);
    return texts;
}

QTEST_MAIN(TestUi)
#include "test_ui.moc"