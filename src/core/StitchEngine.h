#pragma once

#include "utils/PresetData.h"
#include <QImage>
#include <QList>
#include <QObject>
#include <QRect>
#include <QStringList>
#include <QVector>

#include <atomic>

namespace yingtu {

struct StitchSettings {
    enum Direction {
        Vertical,
        Horizontal,
        Grid
    };

    Direction direction = Vertical;
    int spacing = 0;
    QString background = QStringLiteral("transparent");
    QColor bgColor = Qt::white;
    bool uniformWidth = false;
    bool uniformHeight = false;
    bool removeWhiteEdges = false;
    bool autoCropEdges = false;
    int gridRows = 1;
    int gridColumns = 1;
    QString outputFormat = QStringLiteral("png");
    int quality = 90;
    QString outputDir;
    QString baseName = QStringLiteral("stitched");
    QString explicitOutputPath; // 若非空，直接保存到该路径（用户手动选择）
};

class StitchEngine : public QObject
{
    Q_OBJECT
public:
    explicit StitchEngine(QObject* parent = nullptr);

    void setSettings(const StitchSettings& settings) { m_settings = settings; }
    StitchSettings settings() const { return m_settings; }

    /**
     * 取消标志（可选）：process 在开始加载输入前检查。
     * 拼接管线一旦构建无法中断，只能在阶段间退出。
     */
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; }

    // 执行拼接，返回输出文件路径
    QString process(const QStringList& filePaths, bool* ok = nullptr);

    // maxLongEdge 限制**每张输入图**加载时长边的最大像素（目的是限制解码内存，
    // 而不是整张预览的尺寸：例如两张 100x800 纵拼、限 400，得到的是 50x800）。
    // 传 0 表示不限制（仅用于最终输出流程）。
    // inputRects（可选）返回每张输入图在最终拼接图中的实际矩形，用于精确高亮定位。
    static QImage preview(const QStringList& filePaths, const StitchSettings& settings,
                          int maxLongEdge = 0, QVector<QRect>* inputRects = nullptr);

    // 仅读取图片头信息估算最终输出尺寸，不加载像素数据，用于状态栏快速显示。
    static QSize estimateOutputSize(const QStringList& filePaths, const StitchSettings& settings);

    /**
     * 用**已知的图片尺寸**估算输出尺寸，不做任何磁盘 I/O。
     *
     * UI 侧（状态栏）应当使用这个重载：前一个重载要逐个打开文件读图片头，
     * 在主线程上对上百张图调用会造成明显卡顿。
     */
    static QSize estimateOutputSize(const QList<QSize>& sizes, const StitchSettings& settings);

signals:
    void progress(int percent);
    void finished(const QString& outputPath);
    void error(const QString& message);

private:
    StitchSettings m_settings;
    std::atomic<bool>* m_cancelFlag = nullptr;
    bool cancelRequested() const { return m_cancelFlag && m_cancelFlag->load(); }
};

} // namespace yingtu
