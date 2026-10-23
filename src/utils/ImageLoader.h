#pragma once

#include <QImage>
#include <QPixmap>
#include <QString>

#ifdef USE_LIBVIPS
// `_VipsImage` 是 libvips C API 自己的结构体标签名，不能改名；
// 这里只做前向声明以避免把 vips 头文件暴露给所有包含本头的模块。
// NOLINTNEXTLINE
typedef struct _VipsImage VipsImage;
#endif

namespace yingtu {

struct ImageInfo {
    QString filePath;
    int width = 0;
    int height = 0;
    qint64 fileSize = 0;
    QString format;
    QString errorString;
    bool valid = false;
};

class ImageLoader
{
public:
    // 在主线程尽早调用，确保 libvips 从主线程完成一次性初始化，
    // 避免后续在 QtConcurrent 工作线程中首次初始化引发线程/GLib 问题。
    // 返回 false 表示 libvips 初始化失败——此时所有图像操作都会失败，
    // 调用方应当向用户报错，而不是让界面「看起来正常」却处处静默失败。
    static bool initialize();

    static ImageInfo loadInfo(const QString& filePath);

    /**
     * 读取图片信息，**不上报**错误码。
     *
     * 供批量场景使用：一次拖入几百张图时，逐张上报会让日志与用户提示
     * 完全被失败项淹没。由调用方汇总失败项后一次性上报，既有诊断价值，
     * 又不会刷屏。
     */
    static ImageInfo loadInfoQuiet(const QString& filePath);

    static QImage loadImage(const QString& filePath);
    static QImage loadPreview(const QString& filePath, const QSize& maxSize);

    // 直接文件到文件的缩放/压缩/转换，优先使用 libvips 流式处理，避免全图加载。
    // targetSize 为空时保持原始尺寸；format 为空时使用 outputPath 后缀。
    static bool saveThumbnail(const QString& inputPath, const QString& outputPath,
                              const QSize& targetSize = QSize(), const QString& format = QString(),
                              int quality = -1);
    static bool convertFile(const QString& inputPath, const QString& outputPath,
                            const QString& format = QString(), int quality = -1);

    // 任意尺寸缩放（支持拉伸与放大），kernel 对应 VipsKernel：
    // 0=nearest, 1=linear, 2=cubic, 3=lanczos3
    static bool resizeFile(const QString& inputPath, const QString& outputPath,
                           const QSize& targetSize, bool keepAspect = true, int kernel = 1,
                           const QString& format = QString(), int quality = -1);

    // 转换到 sRGB 色彩空间后保存
    static bool convertToSRgbFile(const QString& inputPath, const QString& outputPath,
                                  const QString& format = QString(), int quality = -1);

    // 通过质量迭代逼近目标文件大小（仅支持 jpeg/webp）。
    // targetBytes 为 0 时直接按 quality 保存；targetSize 为空时保持原始尺寸。
    static bool compressToTargetSize(const QString& inputPath, const QString& outputPath,
                                     const QSize& targetSize, qint64 targetBytes,
                                     const QString& format = QString(), int quality = -1);

    // 将内存中的 QImage 通过 libvips（如可用）保存到文件，减少格式插件依赖并统一输出路径。
    // format 为空时使用 outputPath 后缀。
    static bool saveImage(const QImage& image, const QString& outputPath,
                          const QString& format = QString(), int quality = -1);

#ifdef USE_LIBVIPS
    // libvips VipsImage -> QImage 转换（供内部引擎复用）
    static QImage vipsImageToQImage(VipsImage* image);
    // QImage -> libvips VipsImage 转换
    static VipsImage* qImageToVipsImage(const QImage& image);
#endif

private:
    // loadInfo 的内部实现。reportErrors 为 false 时只返回结果，不上报错误码，
    // 供批量场景由调用方汇总后一次性上报。
    static ImageInfo loadInfoImpl(const QString& filePath, bool reportErrors);
};

} // namespace yingtu
