#include <QtCore/qdir.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qstring.h>
#include <QtGui/qfont.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qimage.h>
#include <QtGui/qpainter.h>

static void save(const QString& path, const QImage& img)
{
    QDir().mkpath(QFileInfo(path).path());
    img.save(path);
}

static QImage makeColor(int w, int h, const QColor& c)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    return img;
}

static QImage makeGradient(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB32);
    QPainter p(&img);
    QLinearGradient grad(0, 0, w, h);
    grad.setColorAt(0, QColor(33, 150, 243));
    grad.setColorAt(1, QColor(156, 39, 176));
    p.fillRect(img.rect(), grad);
    return img;
}

static QImage makeStripes(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(QPen(QColor(50, 50, 50), 2));
    for (int x = 0; x < w; x += 20)
        p.drawLine(x, 0, x, h);
    for (int y = 0; y < h; y += 20)
        p.drawLine(0, y, w, y);
    return img;
}

static QImage makeNumbered(int w, int h, int n)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(QColor(240, 240, 240));
    QPainter p(&img);
    p.setPen(Qt::black);
    p.setFont(QFont(QStringLiteral("Arial"), qMin(w, h) / 4));
    p.drawText(img.rect(), Qt::AlignCenter, QString::number(n));
    return img;
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    const QString base = QStringLiteral("tests/fixtures");

    // 单张测试图：不同尺寸与内容
    save(base + QStringLiteral("/sample_800x600_red.jpg"), makeColor(800, 600, Qt::red));
    save(base + QStringLiteral("/sample_800x600_blue.png"), makeColor(800, 600, Qt::blue));
    save(base + QStringLiteral("/sample_600x800_green.png"), makeColor(600, 800, Qt::green));
    save(base + QStringLiteral("/sample_1920x1080_gradient.jpg"), makeGradient(1920, 1080));
    save(base + QStringLiteral("/sample_512x512_stripes.png"), makeStripes(512, 512));

    // 拼接测试序列（横向/纵向/九宫格）
    save(base + QStringLiteral("/stitch/stitch_01.png"), makeColor(400, 300, Qt::red));
    save(base + QStringLiteral("/stitch/stitch_02.png"), makeColor(400, 300, Qt::green));
    save(base + QStringLiteral("/stitch/stitch_03.png"), makeColor(400, 300, Qt::blue));
    save(base + QStringLiteral("/stitch/stitch_04.png"), makeColor(400, 300, Qt::yellow));
    save(base + QStringLiteral("/stitch/stitch_05.png"), makeColor(400, 300, Qt::cyan));
    save(base + QStringLiteral("/stitch/stitch_06.png"), makeColor(400, 300, Qt::magenta));

    // 九宫格编号图
    for (int i = 1; i <= 9; ++i)
        save(base + QStringLiteral("/grid/grid_%1.png").arg(i, 2, 10, QChar('0')), makeNumbered(300, 300, i));

    return 0;
}
