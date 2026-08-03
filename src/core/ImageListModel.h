#pragma once

#include "core/ImageItem.h"
#include <QAbstractListModel>
#include <QFuture>
#include <QFutureWatcher>
#include <QStringList>

namespace yingtu {

struct ImageLoadResult {
    int row = -1;
    ImageInfo info;
    QPixmap thumbnail;
};

struct ImageDataTask {
    int row = -1;
    QString path;
    int thumbnailSize = 120;
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
    void moveItem(int fromRow, int toRow);

    QList<ImageItem> m_items;
    int m_thumbnailSize = 120;
    static constexpr int s_lazyLoadThreshold = 50;

    QList<int> m_imageDataQueue;
    QList<ImageDataTask> m_pendingImageDataTasks;
    QFutureWatcher<ImageLoadResult>* m_imageDataWatcher = nullptr;
    int m_imageDataLoadDone = 0;
    int m_imageDataLoadTotal = 0;
};

} // namespace yingtu
