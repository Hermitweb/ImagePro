#pragma once

#include "core/ImageItem.h"
#include <QAbstractListModel>
#include <QFuture>
#include <QFutureWatcher>
#include <QStringList>

namespace yingtu {

struct ImageLoadResult {
    int row = -1;
    // 用于按身份而非行号回写：加载期间用户可能删除/重排图片，行号会失效。
    QString id;
    ImageInfo info;
    // 必须是 QImage：QPixmap 只能在 GUI 线程创建，工作线程里构造会崩溃
    // 或破坏平台后端状态。由 GUI 线程在 onImageDataLoaded 中再转 QPixmap。
    QImage thumbnail;
};

struct ImageDataTask {
    int row = -1;
    QString id;
    QString path;
    // 与列表单元格实际尺寸（面板宽 200-260px，缩略图约 192-252px）匹配。
    // 原值 120 会让每张缩略图在绘制时被放大，列表明显模糊。
    int thumbnailSize = 240;
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

    int thumbnailSize() const { return m_thumbnailSize; }

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
    void loadImageDataAsync(const QList<int>& rows);
    void processImageDataQueue();
    void onImageDataLoaded(int index);

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

    QList<int> m_imageDataQueue;
    QList<ImageDataTask> m_pendingImageDataTasks;
    QFutureWatcher<ImageLoadResult>* m_imageDataWatcher = nullptr;
    int m_imageDataLoadDone = 0;
    int m_imageDataLoadTotal = 0;

    // 本轮加载中读取失败的图片路径。批量加载时逐张上报会淹没日志与提示，
    // 因此先收集、待本轮结束时汇总成一条错误上报。
    QStringList m_loadFailurePaths;
};

} // namespace yingtu
