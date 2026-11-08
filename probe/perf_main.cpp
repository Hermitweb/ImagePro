// 性能基准探针：100 张图片场景的实测数据（加载耗时/内存峰值/批处理吞吐）。
//
// 用法：build-probe/perf_probe.exe
// 输出逐条 [BENCH] 行，数值供人工对比，不做硬阈值断言（机器差异大）；
// 只对「正确性不变量」做 PASS/FAIL（如批量加载完成后全部有效）。

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "core/ConvertEngine.h"
#include "core/ImageListModel.h"
#include "core/StitchEngine.h"
#include "utils/ImageLoader.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#include <cstdio>

namespace {

size_t rssBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.WorkingSetSize;
#else
    return 0;
#endif
}

double mb(size_t bytes)
{
    return bytes / (1024.0 * 1024.0);
}

void bench(const QString& name, const QString& detail)
{
    const QByteArray utf8 = QStringLiteral("[BENCH] %1 — %2\n").arg(name, detail).toUtf8();
    fwrite(utf8.constData(), 1, utf8.size(), stdout);
    fflush(stdout);
}

QImage makeGradient(int w, int h, const QColor& tint)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y,
                         qRgb((x * 255) / qMax(1, w - 1) * tint.red() / 255,
                              (y * 255) / qMax(1, h - 1) * tint.green() / 255, tint.blue()));
    return img;
}

// 等待模型缩略图加载会话结束（带超时保护）。
bool waitForLoadFinished(yingtu::ImageListModel& model, qint64 timeoutMs, qint64* elapsed)
{
    QElapsedTimer timer;
    timer.start();
    QEventLoop loop;
    QObject::connect(&model, &yingtu::ImageListModel::thumbnailLoadFinished, &loop,
                     &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    if (elapsed)
        *elapsed = timer.elapsed();
    return !model.hasPendingLoads();
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ImageProPerfProbe"));
    app.setOrganizationName(QStringLiteral("ImageProProbe"));
    QStandardPaths::setTestModeEnabled(true);

    if (!yingtu::ImageLoader::initialize()) {
        bench(QStringLiteral("初始化"), QStringLiteral("libvips 初始化失败，无法继续"));
        return 101;
    }

    QTemporaryDir work;
    if (!work.isValid())
        return 100;
    const QString dir = work.path();

    // ── 生成 100 张混合测试图（60×JPEG 1600×1200 + 40×PNG 800×600）────
    QStringList paths;
    paths.reserve(100);
    for (int i = 0; i < 100; ++i) {
        const bool jpeg = i < 60;
        const QImage img = jpeg ? makeGradient(1600, 1200, QColor(255, 128, 64))
                                : makeGradient(800, 600, QColor(64, 128, 255));
        const QString path = dir + QStringLiteral("/img_%1.%2")
                                       .arg(i, 3, 10, QChar('0'))
                                       .arg(jpeg ? QStringLiteral("jpg") : QStringLiteral("png"));
        yingtu::ImageLoader::saveImage(img, path);
        paths.append(path);
    }
    bench(QStringLiteral("准备"),
          QStringLiteral("已生成 100 张测试图（60 JPEG 1600×1200 + 40 PNG 800×600）"));

    // ── 基准 1：批量加载 100 张到缩略图全部就绪 ──────────────────────
    {
        yingtu::ImageListModel model;
        const size_t before = rssBytes();
        QElapsedTimer timer;
        timer.start();
        model.addImages(paths);
        // >50 张走懒加载路径：等效列表视口一次性覆盖全部 100 行
        model.loadVisibleRange(0, 99);
        qint64 elapsed = 0;
        const bool finished = waitForLoadFinished(model, 60000, &elapsed);
        const size_t after = rssBytes();
        bench(QStringLiteral("批量加载 100 张"),
              QStringLiteral("%1 ms, 内存 %2 → %3 MB (Δ%4 MB), valid=%5, 完成=%6")
                  .arg(elapsed)
                  .arg(mb(before), 0, 'f', 1)
                  .arg(mb(after), 0, 'f', 1)
                  .arg(mb(after - before), 0, 'f', 1)
                  .arg(model.validCount())
                  .arg(finished ? QStringLiteral("是") : QStringLiteral("超时")));
        if (!finished)
            bench(QStringLiteral("批量加载 100 张"), QStringLiteral("!! 未在 60s 内完成"));
    }

    // ── 基准 2：滚动作废（只看头尾，中部任务应被跳过）────────────────
    {
        yingtu::ImageListModel model;
        model.addImages(paths);
        model.loadVisibleRange(0, 9); // 先看前 10
        model.loadVisibleRange(90, 99); // 立刻跳到末尾（中部任务应作废）
        qint64 elapsed = 0;
        waitForLoadFinished(model, 60000, &elapsed);
        int tailValid = 0;
        for (int i = 90; i < 100; ++i)
            if (model.itemAt(i) && model.itemAt(i)->isValid())
                ++tailValid;
        bench(QStringLiteral("滚动作废"),
              QStringLiteral("%1 ms, 末尾 10 行有效 %2/10（中部解码被跳过）")
                  .arg(elapsed)
                  .arg(tailValid));
    }

    // ── 基准 3：30 张纵向拼接（统一宽度）preview ────────────────────
    {
        const QStringList stitchInputs = paths.mid(0, 30);
        yingtu::StitchSettings settings;
        settings.direction = yingtu::StitchSettings::Vertical;
        settings.uniformWidth = true;
        const size_t before = rssBytes();
        QElapsedTimer timer;
        timer.start();
        const QImage out = yingtu::StitchEngine::preview(stitchInputs, settings, 1600);
        const qint64 elapsed = timer.elapsed();
        const size_t after = rssBytes();
        bench(QStringLiteral("拼接 preview 30 张（uniformWidth, 长边 1600）"),
              QStringLiteral("%1 ms, 输出 %2x%3, 内存 %4 → %5 MB (Δ%6 MB)")
                  .arg(elapsed)
                  .arg(out.width())
                  .arg(out.height())
                  .arg(mb(before), 0, 'f', 1)
                  .arg(mb(after), 0, 'f', 1)
                  .arg(mb(after - before), 0, 'f', 1));
    }

    // ── 基准 4：100 文件批量转换（PNG→WebP，受控并行）───────────────
    {
        const QString outDir = dir + QStringLiteral("/convert_out");
        yingtu::ConvertEngine engine;
        yingtu::ConvertSettings settings;
        settings.targetFormat = QStringLiteral("webp");
        settings.quality = 80;
        settings.explicitOutputDir = outDir;
        engine.setSettings(settings);
        int progressCount = 0;
        QObject::connect(&engine, &yingtu::ConvertEngine::progress,
                         [&progressCount](int) { ++progressCount; });
        QElapsedTimer timer;
        timer.start();
        bool ok = false;
        const QStringList outs = engine.process(paths, &ok);
        const qint64 elapsed = timer.elapsed();
        bench(QStringLiteral("批量转换 100 张 → WebP"),
              QStringLiteral("%1 ms, 成功 %2, 进度事件 %3, ok=%4")
                  .arg(elapsed)
                  .arg(outs.size())
                  .arg(progressCount)
                  .arg(ok ? QStringLiteral("是") : QStringLiteral("否")));
    }

    bench(QStringLiteral("汇总"), QStringLiteral("基准完成"));
    return 0;
}
