#pragma once

#include <QImage>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace yingtu {

class FileUtils
{
public:
    static QString formatFileSize(qint64 bytes);
    static QStringList supportedImageFormats();
    static QString imageFileFilter();
    static bool isSupportedImage(const QString& filePath);

    /**
     * 把路径列表展开为受支持的图片文件列表：目录递归展开，
     * 文件按扩展名过滤。拖拽与「添加文件夹」共用。
     */
    static QStringList expandToImageFiles(const QStringList& paths);
    static QString generateUniqueOutputPath(const QString& dir, const QString& baseName,
                                            const QString& suffix);

    /**
     * 展开输出文件名模板（不含扩展名）。
     *
     * 占位符：{name} 源文件名 / {time} yyyyMMdd_HHmmss / {date} yyyy-MM-dd /
     * {index} 序号 / {size} WxH / {ext} 扩展名；兼容旧中文占位符。
     * 模板为空时回退 fallbackBase。
     * 此前拼接面板的文件名模板只喂给预览标签，实际输出名从不使用——纯装饰。
     */
    static QString applyFileNameTemplate(const QString& templ, const QString& sourceFileName,
                                         int index, const QSize& size,
                                         const QString& fallbackBase = QStringLiteral("output"));

    /**
     * 原子地预留一个唯一的输出路径。
     *
     * 与 generateUniqueOutputPath 的区别：本函数会**实际创建**一个空文件占位，
     * 用 QIODevice::NewOnly 把「检查存在」与「占用名字」合并为一步原子操作，
     * 因此两个并发运行的批处理任务不可能拿到同一个文件名（消除 TOCTOU 竞态）。
     *
     * 返回值是一个已被占位（0 字节）的文件路径；调用方随后向该路径写入
     * （写入会覆盖占位文件）。若写入失败，必须调用 discardReservedPath()
     * 清理占位，否则会留下 0 字节的「假成功」产物。
     */
    static QString reserveUniqueOutputPath(const QString& dir, const QString& baseName,
                                           const QString& suffix);

    /**
     * 丢弃 reserveUniqueOutputPath 预留但未写入内容的占位文件。
     * 只删除 0 字节文件，写入成功的产物不会被误删。
     */
    static void discardReservedPath(const QString& path);

    static bool saveImage(const QImage& image, const QString& path,
                          const QString& format = QString(), int quality = -1);
    static QPixmap createPlaceholderThumbnail(int size);
};

} // namespace yingtu
