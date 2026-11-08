#pragma once

#include "core/ImageItem.h"
#include <QAbstractListModel>
#include <QMutex>
#include <QSet>
#include <QStringList>

namespace yingtu {

struct ImageLoadResult {
    int row = -1;
    // 用于按身份而非行号回写：加载期间用户可能删除/重排图片，行号会失效。
    QString id;
    ImageInfo info;
    // 必须是 QImage：QPixmap 只能在 GUI 线程创建，工作线程里构造会崩溃
    // 或破坏平台后端状态。由 GUI 线程在 onThumbnailTaskDone 中再转 QPixmap。
    QImage thumbnail;
};

struct ImageDataTask {
    int row = -1;
    QString id;
    QString path;
    // 与列表单元格实际尺寸（面板宽 200-260px，缩略图约 192-252px）匹配。
    // 原值 120 会让每张缩略图在绘制时被放大，列表明显模糊。
    int thumbnailSize = 240;
    /**
     * 是否允许按「想要集合」作废。
     *
     * 由可视范围懒加载（loadVisibleRange）产生的任务为 true：快速滚动时，
     * 已滚出视野的任务在开工前直接跳过，不再浪费解码。
     * 批量添加（≤懒加载阈值）、重试、复制等显式请求产生的任务为 false：
     * 它们是用户明确要的结果，永远执行。
     */
    bool cancelable = false;
};

class ImageListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        FilePathRole,
        DisplayNameRole,
        ThumbnailRole,
        WidthRole,
        HeightRole,
        FileSizeRole,
        FormatRole,
        ValidRole,
        SelectedRole,
        RotationRole,
        HiddenRole,
        ResolutionRole,
        LoadStateRole
    };
    Q_ENUM(Roles)

    explicit ImageListModel(QObject* parent = nullptr);

    void addImage(const QString& filePath);
    void addImages(const QStringList& filePaths);
    void removeImage(int row);
    void removeImages(const QModelIndexList& indexes);
    void moveUp(int row);
    void moveDown(int row);
    void rotateItem(int row);
    void flipHorizontalItem(int row);
    void flipVerticalItem(int row);
    void clear();

    void setHidden(int row, bool hidden);
    void toggleHidden(int row);
    void setHiddenForRows(const QModelIndexList& indexes, bool hidden);
    void moveToFront(const QModelIndexList& indexes);
    void moveToBack(const QModelIndexList& indexes);
    void duplicateItems(const QModelIndexList& indexes);
    void reloadItem(int row);
    void loadVisibleRange(int firstRow, int lastRow, int buffer = 5);

    /** 列表排序字段（右键菜单「排序」）。日期按文件修改时间。 */
    enum class SortField {
        Name,
        FileSize,
        Date,
        Dimensions
    };
    void sortByField(SortField field, Qt::SortOrder order);

    QStringList filePaths() const;
    QStringList allFilePaths() const;
    QStringList visibleFilePaths() const;
    QStringList selectedFilePaths() const;
    int selectedCount() const;
    int validCount() const;
    int visibleCount() const;
    int hiddenCount() const;

    ImageItem* itemAt(int row);
    const ImageItem* itemAt(int row) const;
    int indexOf(const QString& id) const;

    /** 仅供缩略图 worker 调用：循环拉取队列任务直到排空（工作线程）。 */
    void drainThumbnailQueue();

    int thumbnailSize() const { return m_thumbnailSize; }

    /** 还有未完成的缩略图任务吗（供加载动画定时器按需启停）。 */
    bool hasPendingLoads() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

signals:
    void countChanged(int count);
    void selectionChanged();
    void thumbnailLoadStarted(int total);
    void thumbnailLoadProgress(int done, int total);
    void thumbnailLoadFinished();

private:
    void loadImageDataAsync(const QList<int>& rows, bool cancelable);
    /** 从专用线程池补足 worker，直到队列排空（工作共享队列）。 */
    void pumpThumbnailQueue();
    // 以下三个运行在缩略图工作线程 / GUI 线程（由 invokeMethod 决定）：
    void onThumbnailTaskDone(const ImageLoadResult& result);
    void onThumbnailTaskSkipped();
    void onThumbnailWorkerExited();
    /** 单个任务收尾（完成或跳过）：推进度并检查会话是否结束。 */
    void onThumbnailTaskSettled();
    /** 队列空 + 无在途结果 + 无活跃 worker 时收尾：汇总失败并发 Finished。 */
    void settleThumbnailSession();

    /**
     * 本轮加载结束：把收集到的失败项汇总成一条错误上报后清空。
     * 没有失败项时什么都不做。
     */
    void reportCollectedLoadFailures();
    void moveItem(int fromRow, int toRow);

    QList<ImageItem> m_items;
    // 与列表单元格尺寸匹配（见 ImageDataTask::thumbnailSize 的说明）。
    int m_thumbnailSize = 240;
    static constexpr int s_lazyLoadThreshold = 50;

    // ── 缩略图工作共享队列 ──────────────────────────────────────
    // m_imageDataQueue/m_queuedIds/m_wantedIds/m_inFlight/m_activeWorkers
    // 由 m_queueMutex 保护（GUI 线程读写、worker 线程取任务）；
    // m_imageDataLoadDone/Total/m_loadFailurePaths 仅 GUI 线程触碰。
    mutable QMutex m_queueMutex;
    QList<ImageDataTask> m_imageDataQueue;
    QSet<QString> m_queuedIds;
    // 当前可视窗口（+buffer）内待加载图片的 id 集合：
    // loadVisibleRange 每次整体重算，cancelable 任务开工前不在集合内即跳过。
    QSet<QString> m_wantedIds;
    int m_inFlight = 0; // 已解码、待 GUI 回写的任务数
    int m_activeWorkers = 0; // 池中正在拉活的 worker 数
    int m_imageDataLoadDone = 0;
    int m_imageDataLoadTotal = 0;

    // 本轮加载中读取失败的图片路径。批量加载时逐张上报会淹没日志与提示，
    // 因此先收集、待本轮结束时汇总成一条错误上报。
    QStringList m_loadFailurePaths;
};

} // namespace yingtu
