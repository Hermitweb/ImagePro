// 探针式审计主程序：直接驱动 ImageProLib 的真实操作路径，
// 逐条输出 PASS / FAIL / INFO，用于「打通所有操作逻辑」的实测检查。
//
// 与 ctest 的区别：ctest 断言固定回归值；探针断言的是**行为契约**
// （不崩溃、返回值与文档一致、边界输入有干净出路），并主动打印
// 实际观测值供人工判断。
//
// 运行前提：进程旁有 Qt/libvips 运行库（构建脚本已保证）。
// 退出码 = FAIL 条数。

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QRandomGenerator>
#include <QRectF>
#include <QSizeF>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include "core/CompressEngine.h"
#include "core/ConvertEngine.h"
#include "core/EditEngine.h"
#include "core/PdfEngine.h"
#include "core/ResizeEngine.h"
#include "core/StitchEngine.h"
#include "core/WatermarkEngine.h"
#include "utils/EditAction.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include "utils/PresetData.h"
#include "utils/PresetManager.h"

#include <algorithm>
#include <cstdio>

// ── 报告框架 ─────────────────────────────────────────────────────

static int s_pass = 0;
static int s_fail = 0;
static int s_info = 0;
static QString s_section;
static QStringList s_failNames;

static void emitLine(const QString& line)
{
    // 直接写 UTF-8 字节：输出被重定向到文件/管道时不会乱码。
    const QByteArray utf8 = (line + QLatin1Char('\n')).toUtf8();
    fwrite(utf8.constData(), 1, utf8.size(), stdout);
    fflush(stdout);
}

static void section(const QString& title)
{
    s_section = title;
    emitLine(QStringLiteral(""));
    emitLine(QStringLiteral("══ %1 ══").arg(title));
}

static void check(bool ok, const QString& name, const QString& detail = QString())
{
    if (ok) {
        ++s_pass;
        emitLine(QStringLiteral("[PASS] %1 :: %2%3")
                     .arg(s_section, name,
                          detail.isEmpty() ? QString() : QStringLiteral(" — ") + detail));
    } else {
        ++s_fail;
        s_failNames << s_section + QStringLiteral(" :: ") + name;
        emitLine(QStringLiteral("[FAIL] %1 :: %2%3")
                     .arg(s_section, name,
                          detail.isEmpty() ? QString() : QStringLiteral(" — ") + detail));
    }
}

static void info(const QString& name, const QString& detail)
{
    ++s_info;
    emitLine(QStringLiteral("[INFO] %1 :: %2 — %3").arg(s_section, name, detail));
}

// ── 测试图像生成 ─────────────────────────────────────────────────

static QImage solidImage(int w, int h, const QColor& c)
{
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(c);
    return img;
}

static QImage gradientImage(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int r = (x * 255) / qMax(1, w - 1);
            const int g = (y * 255) / qMax(1, h - 1);
            img.setPixel(x, y, qRgb(r, g, 128));
        }
    }
    return img;
}

// 三段 alpha：0 / 128 / 255，各段颜色不同，用于检验 alpha 保真。
static QImage alphaPatternImage(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32);
    const int third = qMax(1, h / 3);
    for (int y = 0; y < h; ++y) {
        const uchar a = y < third ? 0 : (y < 2 * third ? 128 : 255);
        const QColor c = y < third ? QColor(255, 0, 0) : (y < 2 * third ? QColor(0, 255, 0) : QColor(0, 0, 255));
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgba(c.red(), c.green(), c.blue(), a));
    }
    return img;
}

static QImage noisyImage(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB888);
    QRandomGenerator* rng = QRandomGenerator::global();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb(rng->bounded(256), rng->bounded(256), rng->bounded(256)));
    return img;
}

static bool colorNear(const QImage& img, int x, int y, const QColor& expect, int tol = 14,
                      QString* why = nullptr)
{
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) {
        if (why)
            *why = QStringLiteral("采样点越界 (%1,%2) size=%3x%4").arg(x).arg(y).arg(img.width()).arg(img.height());
        return false;
    }
    const QColor c = img.pixelColor(x, y);
    const bool near = qAbs(c.red() - expect.red()) <= tol && qAbs(c.green() - expect.green()) <= tol &&
                      qAbs(c.blue() - expect.blue()) <= tol;
    if (!near && why) {
        *why = QStringLiteral("点(%1,%2) 实际 rgb(%3,%4,%5) 期望 rgb(%6,%7,%8)")
                   .arg(x)
                   .arg(y)
                   .arg(c.red())
                   .arg(c.green())
                   .arg(c.blue())
                   .arg(expect.red())
                   .arg(expect.green())
                   .arg(expect.blue());
    }
    return near;
}

static QImage loadOrNull(const QString& path, const QString& what, bool* okOut)
{
    const QImage img = yingtu::ImageLoader::loadImage(path);
    if (okOut)
        *okOut = !img.isNull();
    if (img.isNull())
        info(what, QStringLiteral("输出无法加载: %1").arg(path));
    return img;
}

// ── 探针主体 ─────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ImageProProbe"));
    app.setOrganizationName(QStringLiteral("ImageProProbe"));
    // 预设/日志等落盘走 QStandardPaths —— 测试模式重定向，绝不碰用户真实配置。
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir work;
    if (!work.isValid()) {
        emitLine(QStringLiteral("FATAL: 无法创建临时工作目录"));
        return 100;
    }
    const QString dir = work.path();
    emitLine(QStringLiteral("工作目录: %1").arg(dir));

    if (!yingtu::ImageLoader::initialize()) {
        emitLine(QStringLiteral("FATAL: libvips 初始化失败，所有图像操作都无法进行"));
        return 101;
    }
    info("libvips 初始化", "initialize() 返回 true");

    // ══ 一、加载 ══════════════════════════════════════════════════
    section(QStringLiteral("加载"));

    // 1.1 奇数宽度 RGB888 往返（H1 行填充回归探针）
    {
        const QImage src = gradientImage(101, 60);
        const QString p = dir + QStringLiteral("/g888.png");
        const bool saved = yingtu::ImageLoader::saveImage(src, p);
        check(saved, "RGB888 保存 PNG", p);
        const QImage back = yingtu::ImageLoader::loadImage(p);
        check(!back.isNull() && back.size() == src.size(), "RGB888 往返尺寸",
              QStringLiteral("%1x%2").arg(back.width()).arg(back.height()));
        bool pixOk = !back.isNull();
        QString why;
        if (pixOk) {
            const int xs[] = { 0, 25, 50, 75, 100 };
            for (int x : xs) {
                if (!colorNear(back, x, 30, src.pixelColor(x, 30), 2, &why)) {
                    pixOk = false;
                    break;
                }
            }
        }
        check(pixOk, "RGB888 往返像素", why);
    }

    // 1.2 alpha 往返（H2 回归探针）
    {
        const QImage src = alphaPatternImage(37, 30);
        const QString p = dir + QStringLiteral("/alpha.png");
        check(yingtu::ImageLoader::saveImage(src, p), "带 alpha 保存 PNG");
        const QImage back = yingtu::ImageLoader::loadImage(p);
        bool aOk = back.format() == QImage::Format_ARGB32 || back.hasAlphaChannel();
        QString why;
        if (aOk && !back.isNull()) {
            const int ys[] = { 2, 15, 28 }; // 三段 alpha 的中点
            const int expectA[] = { 0, 128, 255 };
            for (int i = 0; i < 3; ++i) {
                const int a = back.pixelColor(18, ys[i]).alpha();
                if (qAbs(a - expectA[i]) > 2) {
                    aOk = false;
                    why = QStringLiteral("y=%1 alpha 实际 %2 期望 %3").arg(ys[i]).arg(a).arg(expectA[i]);
                    break;
                }
            }
        } else if (back.isNull()) {
            aOk = false;
            why = QStringLiteral("加载为空");
        }
        check(aOk, "alpha 三段保真 (0/128/255)", why);
    }

    // 1.3 灰度往返
    {
        QImage src(101, 50, QImage::Format_Grayscale8);
        for (int y = 0; y < 50; ++y)
            for (int x = 0; x < 101; ++x)
                src.setPixel(x, y, (x * 255) / 100);
        const QString p = dir + QStringLiteral("/gray.png");
        check(yingtu::ImageLoader::saveImage(src, p), "灰度保存 PNG");
        const QImage back = yingtu::ImageLoader::loadImage(p);
        check(!back.isNull() && back.size() == src.size(), "灰度往返尺寸");
        bool gOk = !back.isNull();
        if (gOk) {
            const QColor a = back.pixelColor(10, 25);
            const QColor b = back.pixelColor(90, 25);
            gOk = a.red() == a.green() && a.green() == a.blue() && b.red() == b.blue() &&
                  b.red() > a.red();
        }
        check(gOk, "灰度往返数值单调");
    }

    // 1.4 loadInfo 信息正确性
    {
        const QImage src = solidImage(320, 200, Qt::magenta);
        const QString p = dir + QStringLiteral("/info.jpg");
        yingtu::ImageLoader::saveImage(src, p, "JPEG", 90);
        const yingtu::ImageInfo inf = yingtu::ImageLoader::loadInfo(p);
        check(inf.valid && inf.width == 320 && inf.height == 200 && !inf.format.isEmpty(),
              "loadInfo 尺寸/格式",
              QStringLiteral("valid=%1 %2x%3 fmt=%4")
                  .arg(inf.valid)
                  .arg(inf.width)
                  .arg(inf.height)
                  .arg(inf.format));
        check(QFile(inf.filePath).size() == QFileInfo(p).size() && inf.fileSize > 0, "loadInfo 文件大小");
    }

    // 1.5 错误路径：损坏 / 缺失 / 空 / 目录 / 扩展名不符
    {
        const QString good = dir + QStringLiteral("/corrupt_src.jpg");
        yingtu::ImageLoader::saveImage(gradientImage(120, 90), good, "JPEG", 90);
        QByteArray bytes;
        {
            QFile f(good);
            f.open(QIODevice::ReadOnly);
            bytes = f.readAll();
        }
        const QString trunc = dir + QStringLiteral("/truncated.jpg");
        {
            QFile f(trunc);
            f.open(QIODevice::WriteOnly);
            f.write(bytes.left(bytes.size() * 2 / 5)); // 截断 60%
        }
        auto inf = yingtu::ImageLoader::loadInfo(trunc);
        // libvips 默认宽容解码截断的 JPEG（补灰而不报错）——记录为观测行为而非缺陷；
        // 若要严格模式可在 vips_jpegload 上加 "fail" 选项（属于产品决策）。
        info("截断 JPEG 宽容解码", QStringLiteral("loadInfo valid=%1, loadImage %2")
                                            .arg(inf.valid)
                                            .arg(yingtu::ImageLoader::loadImage(trunc).isNull()
                                                     ? QStringLiteral("为空")
                                                     : QStringLiteral("返回部分图像")));

        inf = yingtu::ImageLoader::loadInfo(dir + QStringLiteral("/__missing__.png"));
        check(!inf.valid && !inf.errorString.isEmpty(), "缺失文件 loadInfo 报错");

        const QString empty = dir + QStringLiteral("/empty.jpg");
        { QFile f(empty); f.open(QIODevice::WriteOnly); }
        check(yingtu::ImageLoader::loadImage(empty).isNull(), "0 字节文件加载为空（不崩溃）");

        check(yingtu::ImageLoader::loadImage(dir).isNull(), "传入目录路径返回空（不崩溃）");

        // PNG 数据存成 .jpg 扩展名：应能嗅探解码
        const QString lying = dir + QStringLiteral("/actually_png.jpg");
        yingtu::ImageLoader::saveImage(solidImage(50, 40, Qt::cyan), lying, "PNG");
        const QImage lie = yingtu::ImageLoader::loadImage(lying);
        check(!lie.isNull(), "扩展名不符仍可解码", lie.isNull() ? QStringLiteral("返回空") : QStringLiteral("成功"));
    }

    // 1.6 16-bit 灰度 PNG（高位深输入）
    {
        QImage src(64, 48, QImage::Format_Grayscale16);
        src.fill(32768);
        const QString p = dir + QStringLiteral("/gray16.png");
        src.save(p, "PNG");
        const QImage back = yingtu::ImageLoader::loadImage(p);
        check(!back.isNull(), "16-bit 灰度 PNG 可加载",
              back.isNull() ? QStringLiteral("返回空") : QStringLiteral("%1x%2").arg(back.width()).arg(back.height()));
    }

    // 1.7 loadPreview 尺寸约束
    {
        const QString p = dir + QStringLiteral("/big.png");
        yingtu::ImageLoader::saveImage(gradientImage(600, 400), p, "PNG");
        const QImage prev = yingtu::ImageLoader::loadPreview(p, QSize(100, 100));
        check(!prev.isNull() && prev.width() <= 100 && prev.height() <= 100,
              QStringLiteral("loadPreview 长边受限 (%1x%2)").arg(prev.width()).arg(prev.height()));
        check(qAbs(prev.width() - prev.height() * 3 / 2) <= 2, "loadPreview 保持纵横比");
    }

    // ══ 二、保存 ══════════════════════════════════════════════════
    section(QStringLiteral("保存"));

    {
        const QImage src = gradientImage(80, 60);
        const struct { const char* fmt; } fmts[] = { { "png" }, { "jpg" }, { "webp" }, { "tif" }, { "bmp" } };
        for (const auto& f : fmts) {
            const QString p = dir + QStringLiteral("/out.") + f.fmt;
            const bool ok = yingtu::ImageLoader::saveImage(src, p);
            const QImage back = ok ? yingtu::ImageLoader::loadImage(p) : QImage();
            check(ok && !back.isNull() && back.size() == src.size(),
                  QStringLiteral("saveImage → %1").arg(f.fmt),
                  ok ? QStringLiteral("加载回 %1x%2").arg(back.width()).arg(back.height())
                     : QStringLiteral("保存失败"));
        }
    }

    {
        // 未知扩展名：应干净失败而不是产出坏文件
        const QString p = dir + QStringLiteral("/out.unknownxyz");
        const bool ok = yingtu::ImageLoader::saveImage(gradientImage(40, 40), p);
        info("未知扩展名保存", ok ? QStringLiteral("返回 true（有回退）") : QStringLiteral("返回 false"));
        check(!QFileInfo(p).exists() || ok, "未知扩展名不产生半成品文件");
    }

    {
        // 目标是「已存在的普通文件当目录」→ 应干净失败
        const QString blocker = dir + QStringLiteral("/blocker");
        { QFile f(blocker); f.open(QIODevice::WriteOnly); f.write("x"); }
        const bool ok = yingtu::ImageLoader::saveImage(gradientImage(40, 40),
                                                       blocker + QStringLiteral("/sub.png"));
        check(!ok, "目标目录不可创建时保存失败", ok ? QStringLiteral("竟然成功") : QString());
    }

    {
        const QImage src = noisyImage(200, 150);
        const QString hi = dir + QStringLiteral("/q95.jpg");
        const QString lo = dir + QStringLiteral("/q15.jpg");
        yingtu::ImageLoader::saveImage(src, hi, "JPEG", 95);
        yingtu::ImageLoader::saveImage(src, lo, "JPEG", 15);
        check(QFileInfo(hi).size() > QFileInfo(lo).size(), "JPEG 质量参数影响体积",
              QStringLiteral("q95=%1B q15=%2B").arg(QFileInfo(hi).size()).arg(QFileInfo(lo).size()));
    }

    // ══ 三、转换 ══════════════════════════════════════════════════
    section(QStringLiteral("转换"));

    {
        // 带透明 → JPEG（M2 回归探针：4 通道必须压平）
        const QString in = dir + QStringLiteral("/alpha.png"); // 上一节已生成
        const QString out = dir + QStringLiteral("/alpha2jpg.jpg");
        const bool ok = yingtu::ImageLoader::convertFile(in, out, "JPEG", 90);
        check(ok, "透明 PNG → JPEG 成功");
        if (ok) {
            const QImage back = yingtu::ImageLoader::loadImage(out);
            check(!back.isNull() && back.allGray() == false && back.pixelColor(10, 5).alpha() == 255,
                  "转换结果不透明", QStringLiteral("size=%1x%2").arg(back.width()).arg(back.height()));
        }
    }

    {
        yingtu::ConvertEngine eng;
        QStringList errs;
        QObject::connect(&eng, &yingtu::ConvertEngine::error, &eng, [&](const QString& m) { errs << m; });
        yingtu::ConvertSettings cs;
        cs.targetFormat = QStringLiteral("webp");
        cs.quality = 80;
        cs.explicitOutputPath = dir + QStringLiteral("/conv.webp");
        eng.setSettings(cs);
        bool ok = false;
        const QStringList outs = eng.process({ dir + QStringLiteral("/g888.png") }, &ok);
        check(ok && !outs.isEmpty() && QFileInfo(cs.explicitOutputPath).exists(),
              "ConvertEngine PNG → WebP",
              ok ? QString() : errs.join(QStringLiteral("; ")));
        check(yingtu::ConvertEngine::estimateSize(gradientImage(600, 400), cs) > 0, "estimateSize 返回正值");
    }

    // ══ 四、缩放 ══════════════════════════════════════════════════
    section(QStringLiteral("缩放"));

    {
        // M10 回归探针：百分比 0 / 负数必须钳制，不允许无效 QSize
        yingtu::ResizeSettings rs;
        rs.mode = yingtu::ResizeMode::Percentage;
        rs.percentage = 0;
        const QSize zero = yingtu::ResizeEngine::outputSize(QSize(100, 50), rs);
        check(zero.width() >= 1 && zero.height() >= 1, "outputSize(0%) 钳制 ≥1",
              QStringLiteral("得到 %1x%2").arg(zero.width()).arg(zero.height()));
        rs.percentage = -30;
        const QSize neg = yingtu::ResizeEngine::outputSize(QSize(100, 50), rs);
        check(neg.width() >= 1 && neg.height() >= 1, "outputSize(-30%) 钳制 ≥1",
              QStringLiteral("得到 %1x%2").arg(neg.width()).arg(neg.height()));

        // 真实执行 0%：不崩溃，结果要么干净失败要么 ≥1px
        yingtu::ResizeEngine eng;
        rs.percentage = 0;
        rs.explicitOutputPath = dir + QStringLiteral("/resize0.png");
        eng.setSettings(rs);
        bool ok = true;
        QStringList errs;
        QObject::connect(&eng, &yingtu::ResizeEngine::error, &eng, [&](const QString& m) { errs << m; });
        const QStringList outs = eng.process({ dir + QStringLiteral("/g888.png") }, &ok);
        const QImage back = ok && !outs.isEmpty() ? yingtu::ImageLoader::loadImage(outs.first()) : QImage();
        check(!ok || (!back.isNull() && back.width() >= 1 && back.height() >= 1),
              "0% 缩放行为有界",
              ok ? QStringLiteral("成功 %1x%2").arg(back.width()).arg(back.height())
                 : QStringLiteral("报错: %1").arg(errs.value(0)));
    }

    {
        // 等比锁定 + 静态/实际一致性
        const QString in = dir + QStringLiteral("/r300.png");
        yingtu::ImageLoader::saveImage(gradientImage(300, 200), in, "PNG");
        yingtu::ResizeSettings rs;
        rs.mode = yingtu::ResizeMode::Pixel;
        rs.targetWidth = 150;
        rs.targetHeight = 100;
        rs.lockAspectRatio = true;
        const QSize predicted = yingtu::ResizeEngine::outputSize(QSize(300, 200), rs);
        check(predicted == QSize(150, 100), "outputSize 等比预测",
              QStringLiteral("%1x%2").arg(predicted.width()).arg(predicted.height()));

        yingtu::ResizeEngine eng;
        rs.explicitOutputPath = dir + QStringLiteral("/r150.png");
        eng.setSettings(rs);
        bool ok = false;
        QStringList errs;
        QObject::connect(&eng, &yingtu::ResizeEngine::error, &eng, [&](const QString& m) { errs << m; });
        const QStringList outs = eng.process({ in }, &ok);
        const QImage back = ok && !outs.isEmpty() ? yingtu::ImageLoader::loadImage(outs.first()) : QImage();
        check(ok && !back.isNull() && back.size() == predicted, "实际输出与预测一致",
              ok ? QStringLiteral("%1x%2").arg(back.width()).arg(back.height())
                 : errs.join(QStringLiteral("; ")));
    }

    {
        // 放大路径
        const QString in = dir + QStringLiteral("/tiny.png");
        yingtu::ImageLoader::saveImage(solidImage(20, 20, Qt::red), in, "PNG");
        const QString out = dir + QStringLiteral("/tiny_big.png");
        const bool ok = yingtu::ImageLoader::resizeFile(in, out, QSize(200, 200), true, 1, "PNG");
        const QImage back = ok ? yingtu::ImageLoader::loadImage(out) : QImage();
        check(ok && back.size() == QSize(200, 200), "放大 20→200 支持",
              ok ? QStringLiteral("%1x%2").arg(back.width()).arg(back.height()) : QString());
    }

    // ══ 五、压缩 ══════════════════════════════════════════════════
    section(QStringLiteral("压缩"));

    {
        const QString in = dir + QStringLiteral("/noisy.png");
        yingtu::ImageLoader::saveImage(noisyImage(400, 300), in, "PNG");
        const QString out = dir + QStringLiteral("/target.jpg");
        const qint64 target = 30000;
        const bool ok = yingtu::ImageLoader::compressToTargetSize(in, out, QSize(), target, "JPEG");
        const qint64 got = QFileInfo(out).size();
        check(ok && got <= target * 3 / 2 && got > 0, "compressToTargetSize 逼近目标体积",
              QStringLiteral("目标 %1B 实际 %2B").arg(target).arg(got));
        check(yingtu::ImageLoader::loadImage(out).isNull() == false, "压缩产物可加载");
    }

    {
        // M2 回归探针：带透明的图压缩为 JPEG
        const QString out = dir + QStringLiteral("/alpha_target.jpg");
        const bool ok = yingtu::ImageLoader::compressToTargetSize(dir + QStringLiteral("/alpha.png"), out,
                                                                  QSize(), 8000, "JPEG");
        check(ok, "透明 PNG → 目标体积 JPEG", ok ? QString() : QStringLiteral("压缩失败"));
    }

    {
        const QString in = dir + QStringLiteral("/bigq.jpg");
        yingtu::ImageLoader::saveImage(noisyImage(500, 400), in, "JPEG", 98);
        yingtu::CompressEngine eng;
        yingtu::CompressSettings cs;
        cs.mode = yingtu::CompressMode::Quality;
        cs.strength = 60;
        cs.outputFormat = QStringLiteral("JPEG");
        cs.explicitOutputPath = dir + QStringLiteral("/compressed.jpg");
        eng.setSettings(cs);
        const QList<yingtu::CompressResult> results = eng.process({ in });
        bool ok = results.size() == 1 && results.first().success;
        const qint64 before = QFileInfo(in).size();
        const qint64 after = QFileInfo(cs.explicitOutputPath).size();
        check(ok, "CompressEngine 质量模式成功",
              ok ? QStringLiteral("%1B → %2B").arg(before).arg(after)
                 : results.value(0).errorString);
        if (ok) {
            info("压缩率", QStringLiteral("%1%")
                                .arg(before > 0 ? after * 100 / before : -1));
        }
    }

    // ══ 六、拼接 ══════════════════════════════════════════════════
    section(QStringLiteral("拼接"));

    const QString red100x40 = dir + QStringLiteral("/s_red.png");
    const QString blue150x50 = dir + QStringLiteral("/s_blue.png");
    const QString red100x50 = dir + QStringLiteral("/s_red50.png");
    const QString blue80x50 = dir + QStringLiteral("/s_blue80.png");
    const QString blue100x50 = dir + QStringLiteral("/s_blue100.png");
    yingtu::ImageLoader::saveImage(solidImage(100, 40, Qt::red), red100x40, "PNG");
    yingtu::ImageLoader::saveImage(solidImage(150, 50, Qt::blue), blue150x50, "PNG");
    yingtu::ImageLoader::saveImage(solidImage(100, 50, Qt::red), red100x50, "PNG");
    yingtu::ImageLoader::saveImage(solidImage(80, 50, Qt::blue), blue80x50, "PNG");
    yingtu::ImageLoader::saveImage(solidImage(100, 50, Qt::blue), blue100x50, "PNG");

    auto runStitch = [&](const QStringList& inputs, const yingtu::StitchSettings& ss,
                         const QString& out, bool* ok, QString* errs) -> QImage {
        yingtu::StitchEngine eng;
        yingtu::StitchSettings s = ss;
        s.explicitOutputPath = out;
        eng.setSettings(s);
        QObject::connect(&eng, &yingtu::StitchEngine::error, &eng,
                         [errs](const QString& m) { if (errs->isEmpty()) *errs = m; });
        const QString path = eng.process(inputs, ok);
        if (*ok && !QFileInfo::exists(path))
            *errs = QStringLiteral("process 报成功但文件不存在: %1").arg(path);
        return *ok ? yingtu::ImageLoader::loadImage(path) : QImage();
    };

    {
        // C1 回归探针：纵向 + 统一宽度（触发缩放分支；旧代码在此双重释放）
        yingtu::StitchSettings ss;
        ss.direction = yingtu::StitchSettings::Vertical;
        ss.uniformWidth = true;
        bool ok = false;
        QString errs;
        const QImage out = runStitch({ red100x40, blue150x50 }, ss, dir + QStringLiteral("/st_vw.png"), &ok, &errs);
        check(ok && !out.isNull(), "纵向+统一宽度 拼接成功", errs);
        QString why;
        const bool pixelsOk = ok && out.width() >= 148 && out.width() <= 152 &&
                              qAbs(out.height() - 110) <= 4 &&
                              colorNear(out, out.width() / 2, 30, QColor(255, 0, 0), 20, &why) &&
                              colorNear(out, out.width() / 2, out.height() - 20, QColor(0, 0, 255), 20, &why);
        check(pixelsOk, "纵向+统一宽度 尺寸与颜色", why.isEmpty() ? QStringLiteral("%1x%2").arg(out.width()).arg(out.height()) : why);
    }

    {
        // 不缩放路径：同宽纵向
        yingtu::StitchSettings ss;
        ss.direction = yingtu::StitchSettings::Vertical;
        bool ok = false;
        QString errs;
        const QImage out = runStitch({ red100x50, blue100x50 }, ss, dir + QStringLiteral("/st_v.png"), &ok, &errs);
        QString why;
        const bool okPix = ok && out.size() == QSize(100, 100) &&
                           colorNear(out, 50, 25, QColor(255, 0, 0), 10, &why) &&
                           colorNear(out, 50, 75, QColor(0, 0, 255), 10, &why);
        check(okPix, "纵向等宽拼接", errs.isEmpty() ? why : errs);
    }

    {
        // 横向 + 统一高度（等高不缩放）
        yingtu::StitchSettings ss;
        ss.direction = yingtu::StitchSettings::Horizontal;
        ss.uniformHeight = true;
        bool ok = false;
        QString errs;
        const QImage out = runStitch({ red100x50, blue80x50 }, ss, dir + QStringLiteral("/st_h.png"), &ok, &errs);
        QString why;
        const bool okPix = ok && out.size() == QSize(180, 50) &&
                           colorNear(out, 50, 25, QColor(255, 0, 0), 10, &why) &&
                           colorNear(out, 140, 25, QColor(0, 0, 255), 10, &why);
        check(okPix, "横向+统一高度拼接", errs.isEmpty() ? why : errs);
    }

    {
        // 2x2 网格
        const QString g1 = dir + QStringLiteral("/g_r.png");
        const QString g2 = dir + QStringLiteral("/g_g.png");
        const QString g3 = dir + QStringLiteral("/g_b.png");
        const QString g4 = dir + QStringLiteral("/g_w.png");
        yingtu::ImageLoader::saveImage(solidImage(50, 40, Qt::red), g1, "PNG");
        yingtu::ImageLoader::saveImage(solidImage(50, 40, QColor(0, 255, 0)), g2, "PNG");
        yingtu::ImageLoader::saveImage(solidImage(50, 40, Qt::blue), g3, "PNG");
        yingtu::ImageLoader::saveImage(solidImage(50, 40, Qt::white), g4, "PNG");

        yingtu::StitchSettings ss;
        ss.direction = yingtu::StitchSettings::Grid;
        ss.gridRows = 2;
        ss.gridColumns = 2;
        bool ok = false;
        QString errs;
        const QImage out = runStitch({ g1, g2, g3, g4 }, ss, dir + QStringLiteral("/st_grid.png"), &ok, &errs);
        QString why;
        const bool okPix = ok && out.size() == QSize(100, 80) &&
                           colorNear(out, 25, 20, QColor(255, 0, 0), 10, &why) &&
                           colorNear(out, 75, 20, QColor(0, 255, 0), 10, &why) &&
                           colorNear(out, 25, 60, QColor(0, 0, 255), 10, &why) &&
                           colorNear(out, 75, 60, QColor(255, 255, 255), 10, &why);
        check(okPix, "2x2 网格拼接", errs.isEmpty() ? why : errs);
    }

    {
        // 间距 + 背景
        const QString a = dir + QStringLiteral("/sp_a.png");
        const QString b = dir + QStringLiteral("/sp_b.png");
        yingtu::ImageLoader::saveImage(solidImage(50, 30, Qt::red), a, "PNG");
        yingtu::ImageLoader::saveImage(solidImage(50, 30, Qt::blue), b, "PNG");
        yingtu::StitchSettings ss;
        ss.direction = yingtu::StitchSettings::Vertical;
        ss.spacing = 10;
        ss.background = QStringLiteral("white");
        bool ok = false;
        QString errs;
        const QImage out = runStitch({ a, b }, ss, dir + QStringLiteral("/st_sp.png"), &ok, &errs);
        QString why;
        const bool okPix = ok && out.size() == QSize(50, 70) &&
                           colorNear(out, 25, 35, QColor(255, 255, 255), 10, &why) &&
                           colorNear(out, 25, 5, QColor(255, 0, 0), 10, &why);
        check(okPix, "间距 10px 背景白", errs.isEmpty() ? why : errs);
    }

    {
        // 带 alpha 的输入 + 透明背景
        const QString a = dir + QStringLiteral("/al_st.png");
        yingtu::ImageLoader::saveImage(alphaPatternImage(60, 60), a, "PNG");
        yingtu::StitchSettings ss;
        ss.direction = yingtu::StitchSettings::Vertical;
        ss.background = QStringLiteral("transparent");
        bool ok = false;
        QString errs;
        const QImage out = runStitch({ a, red100x50 }, ss, dir + QStringLiteral("/st_al.png"), &ok, &errs);
        check(ok && !out.isNull(), "透明背景 + alpha 输入不崩溃", errs);
    }

    {
        // preview 限制：maxLongEdge 作用于**每张输入图**（限制解码内存），
        // 不是整张预览图的长边。两张 100x800 纵拼、限 400：
        // 每张先缩到 50x400，再拼出 50x800。
        const QString tall = dir + QStringLiteral("/tall.png");
        yingtu::ImageLoader::saveImage(gradientImage(100, 800), tall, "PNG");
        yingtu::StitchSettings ss;
        ss.direction = yingtu::StitchSettings::Vertical;
        const QImage pv = yingtu::StitchEngine::preview({ tall, tall }, ss, 400);
        check(!pv.isNull() && pv.width() == 50 && pv.height() == 800,
              QStringLiteral("preview maxLongEdge 逐输入限制 (%1x%2)").arg(pv.width()).arg(pv.height()));
    }

    {
        // 估算（无 IO 重载）
        yingtu::StitchSettings ss;
        ss.direction = yingtu::StitchSettings::Vertical;
        const QSize est = yingtu::StitchEngine::estimateOutputSize(QList<QSize>{ QSize(100, 50), QSize(100, 50) }, ss);
        check(est == QSize(100, 100), "estimateOutputSize 纵向", QStringLiteral("%1x%2").arg(est.width()).arg(est.height()));
    }

    // ══ 七、水印 ══════════════════════════════════════════════════
    section(QStringLiteral("水印"));

    {
        // M8 回归探针：空文字 / 0 间距不得卡死
        const QImage base = gradientImage(300, 200);
        yingtu::WatermarkSettings ws;
        ws.text = QString();
        QElapsedTimer t;
        t.start();
        const QImage out1 = yingtu::WatermarkEngine::preview(base, ws);
        check(!out1.isNull() && t.elapsed() < 5000, "空文字立即返回",
              QStringLiteral("%1ms").arg(t.elapsed()));

        ws.text = QStringLiteral("A");
        ws.tile = true;
        ws.tileSpacing = 0;
        t.restart();
        const QImage out2 = yingtu::WatermarkEngine::preview(base, ws);
        check(!out2.isNull() && t.elapsed() < 5000, "平铺间距 0 不卡死",
              QStringLiteral("%1ms").arg(t.elapsed()));
    }

    {
        const QImage base = gradientImage(300, 200);
        yingtu::WatermarkSettings ws;
        ws.text = QStringLiteral("影图");
        ws.opacity = 100;
        ws.position = 4; // center
        const QImage marked = yingtu::WatermarkEngine::preview(base, ws);
        check(!marked.isNull() && marked.size() == base.size(), "文字水印尺寸不变");
        // 单像素采样会落在字形间隙上，改用全图差分计数
        int diffPixels = 0;
        for (int y = 0; y < base.height() && diffPixels < 50; y += 2)
            for (int x = 0; x < base.width() && diffPixels < 50; x += 2)
                if (marked.pixelColor(x, y) != base.pixelColor(x, y))
                    ++diffPixels;
        check(diffPixels > 0, "文字水印产生可见差异",
              QStringLiteral("采样差分 %1 像素").arg(diffPixels));

        // 图片水印
        const QString wm = dir + QStringLiteral("/wm.png");
        QImage wImg(40, 40, QImage::Format_ARGB32);
        wImg.fill(Qt::transparent);
        QPainter pp(&wImg);
        pp.fillRect(5, 5, 30, 30, QColor(255, 0, 0, 200));
        pp.end();
        yingtu::ImageLoader::saveImage(wImg, wm, "PNG");
        yingtu::WatermarkSettings iw;
        iw.type = yingtu::WatermarkType::Image;
        iw.imagePath = wm;
        iw.position = 4;
        const QImage out = yingtu::WatermarkEngine::preview(base, iw);
        check(!out.isNull() && out != base, "图片水印生效");
    }

    {
        // 引擎文件路径
        const QString in = dir + QStringLiteral("/wm_in.png");
        yingtu::ImageLoader::saveImage(gradientImage(200, 150), in, "PNG");
        yingtu::WatermarkEngine eng;
        yingtu::WatermarkSettings ws;
        ws.text = QStringLiteral("probe");
        ws.explicitOutputPath = dir + QStringLiteral("/wm_out.png");
        ws.outputFormat = QStringLiteral("png");
        eng.setSettings(ws);
        bool ok = false;
        const QStringList outs = eng.process({ in }, &ok);
        check(ok && !outs.isEmpty() && QFileInfo(ws.explicitOutputPath).exists(),
              "WatermarkEngine 写出文件");
    }

    // ══ 八、PDF 导出 ══════════════════════════════════════════════
    section(QStringLiteral("PDF"));

    {
        yingtu::PdfEngine eng;
        yingtu::PdfSettings ps;
        ps.outputPath = dir + QStringLiteral("/out.pdf");
        eng.setSettings(ps);
        bool ok = false;
        const QString path = eng.process({ red100x40, blue150x50 }, &ok);
        check(ok && QFileInfo(ps.outputPath).exists() && QFileInfo(ps.outputPath).size() > 500,
              "双图导出 PDF",
              ok ? QStringLiteral("%1B").arg(QFileInfo(ps.outputPath).size()) : QStringLiteral("失败"));
        if (ok) {
            QFile f(ps.outputPath);
            f.open(QIODevice::ReadOnly);
            check(f.peek(5) == "%PDF-", "产物是合法 PDF 头");
        }
    }

    {
        yingtu::PdfEngine eng;
        yingtu::PdfSettings ps;
        ps.outputPath = dir + QStringLiteral("/bad.pdf");
        eng.setSettings(ps);
        bool ok = true;
        QStringList errs;
        QObject::connect(&eng, &yingtu::PdfEngine::error, &eng, [&](const QString& m) { errs << m; });
        eng.process({ dir + QStringLiteral("/__no1__.png"), dir + QStringLiteral("/__no2__.png") }, &ok);
        check(!ok, "全部输入不可读时报失败（不假成功）",
              errs.isEmpty() ? QStringLiteral("ok=%1").arg(ok) : errs.join(QStringLiteral("; ")));
    }

    {
        yingtu::PdfEngine eng;
        yingtu::PdfSettings ps;
        ps.layout = yingtu::PdfSettings::Grid2x2;
        ps.outputPath = dir + QStringLiteral("/grid.pdf");
        eng.setSettings(ps);
        bool ok = false;
        eng.process({ red100x40, blue150x50, red100x50, blue80x50 }, &ok);
        check(ok && QFileInfo(ps.outputPath).exists(), "2x2 版式导出");
    }

    // ══ 九、编辑引擎 ══════════════════════════════════════════════
    section(QStringLiteral("编辑"));

    {
        // C2 回归探针：RGB888（非 32 位）底图上直接做马赛克
        const QImage base = gradientImage(101, 60); // RGB888，宽 101 非 4 对齐
        yingtu::EditEngine eng;
        eng.setBaseImage(base);
        yingtu::EditAction m;
        m.id = QStringLiteral("m1");
        m.toolType = yingtu::EditToolType::Mosaic;
        m.mosaicStyle = yingtu::MosaicStyle::Square;
        m.mosaicSize = 20;
        m.bounds = QRectF(10, 10, 40, 30);
        eng.addAction(m);
        QImage out;
        out = eng.renderWithSelection(QString());
        check(!out.isNull() && out.size() == base.size(), "RGB888 底图马赛克不越界",
              QStringLiteral("format=%1").arg(out.format()));

        // 完全越界的马赛克矩形（L1）
        yingtu::EditAction bad = m;
        bad.id = QStringLiteral("m2");
        bad.bounds = QRectF(500, 500, 40, 40);
        eng.addAction(bad);
        out = eng.renderWithSelection(QString());
        check(!out.isNull(), "越界马赛克矩形不崩溃");

        // 滤镜
        yingtu::EditAction f;
        f.id = QStringLiteral("f1");
        f.toolType = yingtu::EditToolType::Filter;
        f.filterType = yingtu::FilterType::Grayscale;
        f.bounds = QRectF(0, 0, 101, 60);
        eng.addAction(f);
        out = eng.renderWithSelection(QString());
        const QColor c = out.pixelColor(50, 30);
        check(qAbs(c.red() - c.green()) < 6 && qAbs(c.green() - c.blue()) < 6,
              "灰度滤镜使区域变灰",
              QStringLiteral("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue()));

        // 动作管理
        check(eng.actions().size() == 3, "actions 计数");
        eng.removeAction(QStringLiteral("m2"));
        check(eng.actions().size() == 2, "removeAction 生效");
        eng.clearActions();
        check(eng.actions().isEmpty(), "clearActions 生效");

        // 文字动作渲染
        yingtu::EditAction t;
        t.id = QStringLiteral("t1");
        t.toolType = yingtu::EditToolType::Text;
        t.text = QStringLiteral("探针");
        t.fontSize = 20;
        t.color = Qt::black;
        t.bounds = QRectF(10, 10, 60, 30);
        eng.addAction(t);
        out = eng.renderWithSelection(QStringLiteral("t1"));
        check(!out.isNull() && out.size() == base.size(), "文字动作渲染（含选中态）");

        info("cropBounds", QStringLiteral("%1 %2 %3 %4")
                               .arg(eng.cropBounds().x())
                               .arg(eng.cropBounds().y())
                               .arg(eng.cropBounds().width())
                               .arg(eng.cropBounds().height()));
    }

    // ══ 十、预设 ══════════════════════════════════════════════════
    section(QStringLiteral("预设"));

    {
        yingtu::PresetManager& mgr = yingtu::PresetManager::instance();
        yingtu::GridPreset p;
        p.id = QStringLiteral("probe-p1");
        p.name = QStringLiteral("探针预设");
        p.rows = 2;
        p.columns = 3;
        p.category = QStringLiteral("Probing");
        p.tags = QStringList{ QStringLiteral("probe") };
        check(mgr.savePreset(p), "savePreset 成功");

        const QList<yingtu::GridPreset> all = mgr.loadPresets();
        const auto it = std::find_if(all.cbegin(), all.cend(),
                                     [](const yingtu::GridPreset& g) { return g.id == QStringLiteral("probe-p1"); });
        check(it != all.cend() && it->rows == 2 && it->columns == 3 && it->category == QStringLiteral("Probing"),
              "预设往返字段完整（含 category）",
              it == all.cend() ? QStringLiteral("未找到")
                               : QStringLiteral("rows=%1 cols=%2 cat=%3").arg(it->rows).arg(it->columns).arg(it->category));
        check(mgr.presetExists(QStringLiteral("探针预设")), "presetExists 按名称命中");

        // M20：无效预设必须被拒绝
        yingtu::GridPreset bad = p;
        bad.id = QStringLiteral("probe-bad");
        bad.rows = 0;
        check(!mgr.savePreset(bad), "rows=0 的预设被拒绝保存");

        // M23：损坏文件 → 写入路径上先备份再覆盖（读取路径刻意不触碰原文件）
        const QString file = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                             QStringLiteral("/grid_presets.json");
        check(QFileInfo(file).exists(), "预设文件落在测试模式配置目录", file);
        {
            QFile f(file);
            f.open(QIODevice::WriteOnly);
            f.write("{ this is not json !!!");
        }
        const QList<yingtu::GridPreset> after = mgr.loadPresets();
        check(!after.isEmpty(), "损坏文件仍返回可用预设列表",
              QStringLiteral("n=%1").arg(after.size()));

        yingtu::GridPreset p2;
        p2.id = QStringLiteral("probe-p2");
        p2.name = QStringLiteral("损坏后写入");
        p2.rows = 1;
        p2.columns = 1;
        check(mgr.savePreset(p2), "损坏状态下仍可保存新预设");
        const QString bak = file + QStringLiteral(".corrupt.bak");
        check(QFileInfo(bak).exists(), "写入时损坏文件已备份 .corrupt.bak");

        // 删除自定义预设
        check(mgr.deletePreset(QStringLiteral("probe-p1")), "deletePreset 成功");
        check(!mgr.presetExists(QStringLiteral("探针预设")), "删除后 presetExists 为 false");
    }

    // ══ 十一、文件工具 ════════════════════════════════════════════
    section(QStringLiteral("文件工具"));

    {
        const QString outDir = dir + QStringLiteral("/uniq");
        QDir().mkpath(outDir);
        const QString p1 = yingtu::FileUtils::generateUniqueOutputPath(outDir, QStringLiteral("img"), QStringLiteral("png"));
        check(!p1.isEmpty() && !QFileInfo::exists(p1), "generateUniqueOutputPath 首次不冲突", p1);
        { QFile f(p1); f.open(QIODevice::WriteOnly); f.write("x"); }
        const QString p2 = yingtu::FileUtils::generateUniqueOutputPath(outDir, QStringLiteral("img"), QStringLiteral("png"));
        check(!p2.isEmpty() && p2 != p1, "已占用时给出新名字", p2);

        // 原子占位
        const QString r1 = yingtu::FileUtils::reserveUniqueOutputPath(outDir, QStringLiteral("res"), QStringLiteral("png"));
        const QString r2 = yingtu::FileUtils::reserveUniqueOutputPath(outDir, QStringLiteral("res"), QStringLiteral("png"));
        check(!r1.isEmpty() && !r2.isEmpty() && r1 != r2, "reserveUniqueOutputPath 并发不重名");
        {
            QFile f(r2);
            f.open(QIODevice::WriteOnly);
            f.write("real data");
        }
        yingtu::FileUtils::discardReservedPath(r2);
        check(QFileInfo(r2).exists(), "discardReservedPath 不误删已写入文件");
        yingtu::FileUtils::discardReservedPath(r1);
        check(!QFileInfo(r1).exists(), "discardReservedPath 清理 0 字节占位");
    }

    {
        const QStringList fmts = yingtu::FileUtils::supportedImageFormats();
        check(fmts.size() >= 5, "supportedImageFormats 覆盖常用格式",
              fmts.join(QStringLiteral(",")));
        check(yingtu::FileUtils::isSupportedImage(dir + QStringLiteral("/a.PNG")) &&
                  !yingtu::FileUtils::isSupportedImage(dir + QStringLiteral("/a.xyz")),
              "isSupportedImage 大小写不敏感且拒绝未知");
        check(yingtu::FileUtils::formatFileSize(1536).contains(QStringLiteral("1.5")),
              "formatFileSize 可读", yingtu::FileUtils::formatFileSize(1536));
    }

    // ── 汇总 ──────────────────────────────────────────────────────
    emitLine(QStringLiteral(""));
    emitLine(QStringLiteral("════════ 探针汇总 ════════"));
    emitLine(QStringLiteral("PASS %1 / FAIL %2 / INFO %3").arg(s_pass).arg(s_fail).arg(s_info));
    for (const QString& f : s_failNames)
        emitLine(QStringLiteral("  FAIL → %1").arg(f));
    emitLine(QStringLiteral("配置目录（测试模式）: %1")
                 .arg(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)));
    return s_fail;
}
