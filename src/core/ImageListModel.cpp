#include "ImageListModel.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QFileInfo>
#include <QMimeData>
#include <QtConcurrent>
#include <QUrl>
#include <functional>

namespace yingtu {

ImageListModel::ImageListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_imageDataWatcher = new QFutureWatcher<ImageLoadResult>(this);
    connect(m_imageDataWatcher, &QFutureWatcher<ImageLoadResult>::resultReadyAt,
            this, &ImageListModel::onImageDataLoaded);
    connect(m_imageDataWatcher, &QFutureWatcher<ImageLoadResult>::finished,
            this, &ImageListModel::processImageDataQueue);
}

int ImageListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant ImageListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const ImageItem& item = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return item.displayName();
    case Qt::ToolTipRole:
        return QStringLiteral("%1\n%2")
            .arg(item.displayName())
            .arg(FileUtils::formatFileSize(item.fileSize()));
    case IdRole:
        return item.id();
    case FilePathRole:
        return item.filePath();
    case WidthRole:
        return item.width();
    case HeightRole:
        return item.height();
    case FileSizeRole:
        return item.fileSize();
    case FormatRole:
        return item.format();
    case ValidRole:
        return item.isValid();
    case SelectedRole:
        return item.isSelected();
    case RotationRole:
        return item.rotation();
    case ThumbnailRole:
        return item.thumbnail(m_thumbnailSize);
    default:
        return QVariant();
    }
}

bool ImageListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return false;

    ImageItem& item = m_items[index.row()];
    if (role == SelectedRole) {
        item.setSelected(value.toBool());
        emit dataChanged(index, index, { SelectedRole });
        emit selectionChanged();
        return true;
    }
    return false;
}

Qt::ItemFlags ImageListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return QAbstractListModel::flags(index) | Qt::ItemIsDropEnabled;
    return QAbstractListModel::flags(index) | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QHash<int, QByteArray> ImageListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[FilePathRole] = "filePath";
    roles[DisplayNameRole] = "displayName";
    roles[WidthRole] = "width";
    roles[HeightRole] = "height";
    roles[FileSizeRole] = "fileSize";
    roles[FormatRole] = "format";
    roles[ValidRole] = "valid";
    roles[SelectedRole] = "selected";
    roles[RotationRole] = "rotation";
    roles[ThumbnailRole] = "thumbnail";
    return roles;
}

Qt::DropActions ImageListModel::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

QMimeData* ImageListModel::mimeData(const QModelIndexList& indexes) const
{
    QMimeData* mime = new QMimeData();
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    for (const QModelIndex& idx : indexes) {
        if (idx.isValid()) {
            stream << idx.row();
            stream << m_items.at(idx.row()).id();
        }
    }
    mime->setData(QStringLiteral("application/x-imagepro-row"), encoded);
    return mime;
}

QStringList ImageListModel::mimeTypes() const
{
    return QStringList() << QStringLiteral("application/x-imagepro-row") << QStringLiteral("text/uri-list");
}

bool ImageListModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                  const QModelIndex& parent)
{
    Q_UNUSED(column)
    if (action == Qt::IgnoreAction)
        return true;

    // 内部移动
    if (data->hasFormat(QStringLiteral("application/x-imagepro-row"))) {
        QByteArray encoded = data->data(QStringLiteral("application/x-imagepro-row"));
        QDataStream stream(&encoded, QIODevice::ReadOnly);
        int fromRow = -1;
        QString id;
        stream >> fromRow >> id;
        if (fromRow < 0 || fromRow >= m_items.size() || m_items.at(fromRow).id() != id)
            return false;

        int toRow = row;
        if (toRow < 0 || toRow > m_items.size())
            toRow = m_items.size();
        if (toRow == fromRow || toRow == fromRow + 1)
            return false;

        moveItem(fromRow, toRow > fromRow ? toRow - 1 : toRow);
        return true;
    }

    // 外部文件拖入
    if (data->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : data->urls()) {
            QString path = url.toLocalFile();
            if (FileUtils::isSupportedImage(path))
                paths.append(path);
        }
        if (!paths.isEmpty()) {
            addImages(paths);
            return true;
        }
    }

    return QAbstractListModel::dropMimeData(data, action, row, column, parent);
}

void ImageListModel::addImage(const QString& filePath)
{
    if (!FileUtils::isSupportedImage(filePath))
        return;

    int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.append(ImageItem(filePath, false));
    endInsertRows();
    emit countChanged(m_items.size());

    loadImageDataAsync({ row });
}

void ImageListModel::addImages(const QStringList& filePaths)
{
    if (filePaths.isEmpty())
        return;

    int row = m_items.size();
    int count = 0;
    for (const QString& path : filePaths) {
        if (FileUtils::isSupportedImage(path))
            ++count;
    }
    if (count == 0)
        return;

    beginInsertRows(QModelIndex(), row, row + count - 1);
    QList<int> newRows;
    for (const QString& path : filePaths) {
        if (FileUtils::isSupportedImage(path)) {
            m_items.append(ImageItem(path, false));
            newRows.append(m_items.size() - 1);
        }
    }
    endInsertRows();
    emit countChanged(m_items.size());

    loadImageDataAsync(newRows);
}

void ImageListModel::removeImage(int row)
{
    if (row < 0 || row >= m_items.size())
        return;

    beginRemoveRows(QModelIndex(), row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged(m_items.size());
    emit selectionChanged();
}

void ImageListModel::removeImages(const QModelIndexList& indexes)
{
    QList<int> rows;
    for (const QModelIndex& idx : indexes) {
        if (idx.isValid())
            rows.append(idx.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        removeImage(row);
}

void ImageListModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged(0);
    emit selectionChanged();
}

ImageItem* ImageListModel::itemAt(int row)
{
    if (row < 0 || row >= m_items.size())
        return nullptr;
    return &m_items[row];
}

const ImageItem* ImageListModel::itemAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return nullptr;
    return &m_items[row];
}

int ImageListModel::indexOf(const QString& id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id() == id)
            return i;
    }
    return -1;
}

void ImageListModel::moveItem(int fromRow, int toRow)
{
    if (fromRow < 0 || fromRow >= m_items.size() || toRow < 0 || toRow >= m_items.size() || fromRow == toRow)
        return;

    if (!beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), toRow > fromRow ? toRow + 1 : toRow))
        return;

    m_items.move(fromRow, toRow);
    endMoveRows();
}

void ImageListModel::moveUp(int row)
{
    if (row > 0)
        moveItem(row, row - 1);
}

void ImageListModel::moveDown(int row)
{
    if (row >= 0 && row < m_items.size() - 1)
        moveItem(row, row + 1);
}

void ImageListModel::rotateItem(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    m_items[row].rotate90();
    emit dataChanged(index(row), index(row), { RotationRole, ThumbnailRole });
}

void ImageListModel::flipHorizontalItem(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    m_items[row].setFlippedHorizontal(!m_items[row].flippedHorizontal());
    emit dataChanged(index(row), index(row), { ThumbnailRole });
}

void ImageListModel::flipVerticalItem(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    m_items[row].setFlippedVertical(!m_items[row].flippedVertical());
    emit dataChanged(index(row), index(row), { ThumbnailRole });
}

QStringList ImageListModel::filePaths() const
{
    QStringList paths;
    for (const auto& item : m_items)
        paths.append(item.filePath());
    return paths;
}

int ImageListModel::selectedCount() const
{
    int count = 0;
    for (const auto& item : m_items) {
        if (item.isSelected())
            ++count;
    }
    return count;
}

int ImageListModel::validCount() const
{
    int count = 0;
    for (const auto& item : m_items) {
        if (item.isValid())
            ++count;
    }
    return count;
}

static ImageLoadResult loadImageDataTask(const QPair<int, QString>& task)
{
    ImageLoadResult result;
    result.row = task.first;
    result.info = ImageLoader::loadInfo(task.second);
    if (result.info.valid)
        result.thumbnail = ImageLoader::loadThumbnail(task.second, 64);
    return result;
}

void ImageListModel::loadImageDataAsync(const QList<int>& rows)
{
    if (rows.isEmpty())
        return;

    // 去重合并到队列
    for (int row : rows) {
        if (row >= 0 && row < m_items.size()
            && !m_items.at(row).isValid()
            && !m_imageDataQueue.contains(row)) {
            m_imageDataQueue.append(row);
        }
    }

    if (m_imageDataWatcher->isRunning())
        return;

    processImageDataQueue();
}

void ImageListModel::processImageDataQueue()
{
    if (m_imageDataQueue.isEmpty()) {
        if (m_pendingImageDataTasks.isEmpty()) {
            m_imageDataLoadTotal = 0;
            m_imageDataLoadDone = 0;
            emit thumbnailLoadFinished();
        }
        return;
    }

    m_pendingImageDataTasks.clear();
    for (int row : m_imageDataQueue) {
        if (row >= 0 && row < m_items.size() && !m_items.at(row).isValid())
            m_pendingImageDataTasks.append(qMakePair(row, m_items.at(row).filePath()));
    }
    m_imageDataQueue.clear();

    if (m_pendingImageDataTasks.isEmpty()) {
        emit thumbnailLoadFinished();
        return;
    }

    if (m_imageDataLoadTotal == 0)
        m_imageDataLoadTotal = m_pendingImageDataTasks.size();
    m_imageDataLoadDone = 0;
    emit thumbnailLoadStarted(m_imageDataLoadTotal);

    QFuture<ImageLoadResult> future = QtConcurrent::mapped(m_pendingImageDataTasks, loadImageDataTask);
    m_imageDataWatcher->setFuture(future);
}

void ImageListModel::onImageDataLoaded(int index)
{
    if (index < 0 || index >= m_pendingImageDataTasks.size())
        return;

    ImageLoadResult result = m_imageDataWatcher->resultAt(index);
    int row = result.row;
    if (row < 0 || row >= m_items.size())
        return;

    ImageItem& item = m_items[row];
    item.setInfo(result.info);
    if (!result.thumbnail.isNull())
        item.setThumbnail(result.thumbnail);

    emit dataChanged(this->index(row), this->index(row),
                     { WidthRole, HeightRole, FileSizeRole, FormatRole, ValidRole, ThumbnailRole });

    ++m_imageDataLoadDone;
    emit thumbnailLoadProgress(m_imageDataLoadDone, m_imageDataLoadTotal);
}

} // namespace yingtu
