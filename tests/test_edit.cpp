#include "core/EditEngine.h"
#include "core/MosaicFilter.h"
#include "utils/EditAction.h"

#include <QImage>
#include <QTest>

using namespace yingtu;

// 编辑引擎的回归保护。
//
// 重点覆盖 C2（马赛克写入非 32 位图像造成堆缓冲区溢出）：
//   ImageLoader 对 3 通道文件返回 Format_RGB888（每像素 3 字节），
//   而 applyMosaic 用 QRgb*（每像素 4 字节）直接写 scanLine，
//   只要 width*3 % 4 != 0 就会每行越界写约 width 字节 —— 堆损坏。
// 修复方式是 applyMosaic 入口做防御性格式归一，EditEngine::renderWithSelection
// 同样统一到 ARGB32。

class TestEdit : public QObject
{
    Q_OBJECT

private slots:
    void testMosaicOnRgb888DoesNotOverflow();
    void testMosaicOnGrayscaleDoesNotOverflow();
    void testMosaicOnArgb32PreservesSize();
    void testMosaicOnEmptyImage();
    void testRenderRectangleChangesPixels();
    void testRenderWithoutBaseImageIsUsable();
    void testAddRemoveUpdateActions();
    void testClearActionsResetsRender();
    void testSetActionsReplacesList();
    void testCropBounds();
    void testHitTestAndHandleRect();
    void testGrayscaleFilterApplied();
    void testTextAnnotationRendered();
};

namespace {

// 宽度取 101：101*3 = 303 不是 4 的倍数，Qt 会在每行末尾补 1 字节对齐填充。
// 这正是最容易触发 RGB888 行越界的宽度。
constexpr int kOddWidth = 101;
constexpr int kHeight = 60;

QImage makeRgb888Image()
{
    QImage img(kOddWidth, kHeight, QImage::Format_RGB888);
    for (int y = 0; y < kHeight; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < kOddWidth; ++x) {
            line[x * 3 + 0] = static_cast<uchar>((x * 5 + y * 3) % 256);
            line[x * 3 + 1] = static_cast<uchar>((x * 9 + y * 7) % 256);
            line[x * 3 + 2] = static_cast<uchar>((x * 13 + y * 11) % 256);
        }
    }
    return img;
}

EditAction makeMosaicAction()
{
    EditAction action;
    action.id = QStringLiteral("mosaic-1");
    action.toolType = EditToolType::Mosaic;
    action.mosaicStyle = MosaicStyle::Square;
    action.mosaicSize = 10;
    action.bounds = QRectF(5, 5, 60, 40);
    return action;
}

} // namespace

void TestEdit::testMosaicOnRgb888DoesNotOverflow()
{
    const QImage src = makeRgb888Image();
    const QImage out = applyMosaic(src, makeMosaicAction());

    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), src.size());
    // 输出必须仍是可安全按 QRgb* 访问的 32 位格式，否则调用方继续写会越界。
    QCOMPARE(out.format(), QImage::Format_ARGB32);
}

void TestEdit::testMosaicOnGrayscaleDoesNotOverflow()
{
    QImage src(kOddWidth, kHeight, QImage::Format_Grayscale8);
    src.fill(128);

    const QImage out = applyMosaic(src, makeMosaicAction());
    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), src.size());
    QCOMPARE(out.format(), QImage::Format_ARGB32);
}

void TestEdit::testMosaicOnArgb32PreservesSize()
{
    QImage src(kOddWidth, kHeight, QImage::Format_ARGB32);
    src.fill(Qt::red);

    const QImage out = applyMosaic(src, makeMosaicAction());
    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), src.size());
    // 马赛克区域内像素应与原图不同（被块状化），区域外应基本保持。
    QCOMPARE(out.pixel(2, 2), src.pixel(2, 2));
}

void TestEdit::testMosaicOnEmptyImage()
{
    const QImage out = applyMosaic(QImage(), makeMosaicAction());
    QVERIFY(out.isNull());
}

void TestEdit::testRenderRectangleChangesPixels()
{
    EditEngine engine;
    QImage base(120, 90, QImage::Format_RGB32);
    base.fill(Qt::white);
    engine.setBaseImage(base);

    EditAction rect;
    rect.id = QStringLiteral("rect-1");
    rect.toolType = EditToolType::Rectangle;
    rect.bounds = QRectF(20, 20, 40, 30);
    rect.color = Qt::red;
    rect.fillStyle = EditFillStyle::SolidFill;
    rect.opacity = 100;
    engine.addAction(rect);

    const QImage out = engine.render();
    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), base.size());

    // 矩形内部的像素应被填充为红色。
    const QRgb inside = out.pixel(40, 35);
    QVERIFY2(qRed(inside) > 200 && qGreen(inside) < 60 && qBlue(inside) < 60,
             "rectangle annotation should paint red inside its bounds");
    // 矩形外部应保持白色。
    QCOMPARE(out.pixel(5, 5), base.pixel(5, 5));
}

void TestEdit::testRenderWithoutBaseImageIsUsable()
{
    // 无底图时应返回一张可用的白色画布，而不是未初始化像素。
    EditEngine engine;
    const QImage out = engine.render();
    QVERIFY(!out.isNull());
    QCOMPARE(out.format(), QImage::Format_ARGB32);
    QCOMPARE(out.pixel(0, 0), qRgba(255, 255, 255, 255));
}

void TestEdit::testAddRemoveUpdateActions()
{
    EditEngine engine;
    QImage base(80, 60, QImage::Format_RGB32);
    base.fill(Qt::white);
    engine.setBaseImage(base);

    EditAction a;
    a.id = QStringLiteral("a");
    a.toolType = EditToolType::Rectangle;
    a.bounds = QRectF(0, 0, 10, 10);
    engine.addAction(a);
    QCOMPARE(engine.actions().size(), 1);

    EditAction b = a;
    b.id = QStringLiteral("b");
    b.bounds = QRectF(20, 20, 10, 10);
    engine.addAction(b);
    QCOMPARE(engine.actions().size(), 2);

    // 更新：改 bounds 后内容应生效。
    EditAction updated = b;
    updated.bounds = QRectF(40, 40, 20, 20);
    updated.color = Qt::blue;
    updated.fillStyle = EditFillStyle::SolidFill;
    updated.opacity = 100;
    engine.updateAction(updated);
    QCOMPARE(engine.actions().size(), 2);
    QCOMPARE(engine.actions().at(1).bounds, QRectF(40, 40, 20, 20));

    // 删除。
    engine.removeAction(QStringLiteral("a"));
    QCOMPARE(engine.actions().size(), 1);
    QCOMPARE(engine.actions().first().id, QStringLiteral("b"));

    // 删除不存在的 id 不应改变列表。
    engine.removeAction(QStringLiteral("does-not-exist"));
    QCOMPARE(engine.actions().size(), 1);
}

void TestEdit::testClearActionsResetsRender()
{
    EditEngine engine;
    QImage base(60, 60, QImage::Format_RGB32);
    base.fill(Qt::white);
    engine.setBaseImage(base);

    EditAction rect;
    rect.id = QStringLiteral("r");
    rect.toolType = EditToolType::Rectangle;
    rect.bounds = QRectF(10, 10, 30, 30);
    rect.color = Qt::red;
    rect.fillStyle = EditFillStyle::SolidFill;
    rect.opacity = 100;
    engine.addAction(rect);

    QVERIFY(engine.render().pixel(25, 25) != engine.baseImage().pixel(25, 25));

    engine.clearActions();
    QVERIFY(engine.actions().isEmpty());
    // 清除标注后渲染结果应与底图一致。
    QCOMPARE(engine.render().pixel(25, 25), base.pixel(25, 25));
}

void TestEdit::testSetActionsReplacesList()
{
    EditEngine engine;

    EditAction a;
    a.id = QStringLiteral("a");
    a.toolType = EditToolType::Ellipse;
    a.bounds = QRectF(0, 0, 10, 10);

    EditAction b;
    b.id = QStringLiteral("b");
    b.toolType = EditToolType::Text;
    b.bounds = QRectF(20, 20, 30, 30);

    engine.setActions(QList<EditAction>() << a << b);
    QCOMPARE(engine.actions().size(), 2);

    // 再次调用应整体替换，而不是追加。
    engine.setActions(QList<EditAction>() << b);
    QCOMPARE(engine.actions().size(), 1);
    QCOMPARE(engine.actions().first().id, QStringLiteral("b"));
}

void TestEdit::testCropBounds()
{
    EditEngine engine;

    // 没有裁剪动作时返回空矩形。
    QVERIFY(engine.cropBounds().isEmpty());

    EditAction crop;
    crop.id = QStringLiteral("crop");
    crop.toolType = EditToolType::Crop;
    crop.bounds = QRectF(10, 20, 100, 50);
    engine.addAction(crop);

    const QRectF bounds = engine.cropBounds();
    QCOMPARE(bounds.left(), 10.0);
    QCOMPARE(bounds.top(), 20.0);
    QCOMPARE(bounds.width(), 100.0);
    QCOMPARE(bounds.height(), 50.0);
}

void TestEdit::testHitTestAndHandleRect()
{
    const QRectF bounds(0, 0, 100, 100);

    // 中心附近应命中整体（handleIndex = -1）。
    QCOMPARE(EditEngine::hitTest(bounds, QPointF(50, 50)), -1);
    // 明显在外部则完全不命中（-2）。
    QCOMPARE(EditEngine::hitTest(bounds, QPointF(300, 300)), -2);

    // 左上角控制点。
    const QRectF handle = EditEngine::handleRect(bounds, 0);
    QVERIFY(!handle.isEmpty());
    const int idx = EditEngine::hitTest(bounds, handle.center());
    QCOMPARE(idx, 0);
}

void TestEdit::testGrayscaleFilterApplied()
{
    EditEngine engine;
    QImage base(40, 40, QImage::Format_RGB32);
    base.fill(QColor(200, 30, 30));
    engine.setBaseImage(base);

    EditAction filter;
    filter.id = QStringLiteral("gray");
    filter.toolType = EditToolType::Filter;
    filter.filterType = FilterType::Grayscale;
    engine.addAction(filter);

    const QImage out = engine.render();
    QVERIFY(!out.isNull());
    const QRgb p = out.pixel(20, 20);
    // 灰度化后 R≈G≈B。
    QVERIFY2(qAbs(qRed(p) - qGreen(p)) <= 2 && qAbs(qGreen(p) - qBlue(p)) <= 2,
             "grayscale filter should produce near-equal channels");
}

void TestEdit::testTextAnnotationRendered()
{
    EditEngine engine;
    QImage base(200, 100, QImage::Format_RGB32);
    base.fill(Qt::white);
    engine.setBaseImage(base);

    EditAction text;
    text.id = QStringLiteral("text-1");
    text.toolType = EditToolType::Text;
    text.text = QStringLiteral("HELLO");
    text.bounds = QRectF(20, 30, 160, 40);
    text.color = Qt::black;
    text.fontSize = 28;
    text.opacity = 100;
    engine.addAction(text);

    const QImage out = engine.render();
    QVERIFY(!out.isNull());

    // 文本区域应有非白色像素（文字被画上去了）。
    bool foundInk = false;
    for (int y = 30; y < 70 && !foundInk; ++y) {
        for (int x = 20; x < 180; ++x) {
            if (qRed(out.pixel(x, y)) < 200) {
                foundInk = true;
                break;
            }
        }
    }
    QVERIFY2(foundInk, "text annotation should paint visible glyphs");
}

QTEST_MAIN(TestEdit)
#include "test_edit.moc"
