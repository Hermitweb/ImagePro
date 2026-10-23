#include "ImageListModel.h"
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include <QDataStream>
#include <QFileInfo>
#include <QMimeData>
#include <QtConcurrent>
#include <QUrl>
#include <algorithm>

namespace yingtu {

ImageListModel::ImageListModel(QObject* parent) : QAbstractListModel(parent)
{
    // 缩略图尺寸与列表单元格实际尺寸匹配（见 ImageDataTask::thumbnailSize 的说明）。
    // 原先在此硬编码 120，会让每张缩略图在绘制时被放大、列表明显模糊。
    m_thumbnailSize = 240;
    m_imageDataWatcher = new QFutureWatcher<ImageLoadResult>(this);
    connect(m_imageDataWatcher, &QFutureWatcher<ImageLoadResult>::resultReadyAt, this,
            &ImageListModel::onImageDataLoaded);
    connect(m_imageDataWatcher, &QFutureWatcher<ImageLoadResult>::finished, this,
            &ImageListModel::processImageDataQueue);
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
    case Qt::ToolTipRole: {
        QString sizeStr = FileUtils::formatFileSize(item.fileSize());
        return QStringLiteral("%1\n%2").arg(item.displayName()).arg(sizeStr);
    }
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
    case HiddenRole:
        return item.isHidden();
    case ResolutionRole:
        return item.resolutionString();
    case LoadStateRole:
        return static_cast<int>(item.loadState());
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
        emit dataChanged(index, index, {SelectedRole});
        emit selectionChanged();
        return true;
    }
    if (role == HiddenRole) {
        item.setHidden(value.toBool());
        emit dataChanged(index, index, {HiddenRole, ThumbnailRole});
        return true;
    }
    return false;
}

Qt::ItemFlags ImageListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return QAbstractListModel::flags(index) | Qt::ItemIsDropEnabled;
    return QAbstractListModel::flags(index) | Qt::ItemIsEnabled | Qt::ItemIsSelectable |
           Qt::ItemIsDragEnabled;
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
    roles[HiddenRole] = "hidden";
    roles[ResolutionRole] = "resolution";
    roles[LoadStateRole] = "loadState";
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
    return QStringList() << QStringLiteral("application/x-imagepro-row")
                         << QStringLiteral("text/uri-list");
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

    if (m_items.size() <= s_lazyLoadThreshold)
        loadImageDataAsync({row});
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

    if (m_items.size() <= s_lazyLoadThreshold)
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

void ImageListModel::setHidden(int row, bool hidden)
{
    if (row < 0 || row >= m_items.size())
        return;
    m_items[row].setHidden(hidden);
    emit dataChanged(index(row), index(row), {HiddenRole, ThumbnailRole});
}

void ImageListModel::toggleHidden(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    setHidden(row, !m_items[row].isHidden());
}

void ImageListModel::setHiddenForRows(const QModelIndexList& indexes, bool hidden)
{
    for (const QModelIndex& idx : indexes) {
        if (idx.isValid())
            setHidden(idx.row(), hidden);
    }
}

void ImageListModel::moveToFront(const QModelIndexList& indexes)
{
    QList<int> rows;
    for (const QModelIndex& idx : indexes) {
        if (idx.isValid())
            rows.append(idx.row());
    }
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        moveItem(row, 0);
}

void ImageListModel::moveToBack(const QModelIndexList& indexes)
{
    QList<int> rows;
    for (const QModelIndex& idx : indexes) {
        if (idx.isValid())
            rows.append(idx.row());
    }
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end());
    int moved = 0;
    for (int row : rows) {
        int current = row - moved;
        if (current >= 0 && current < m_items.size())
            moveItem(current, m_items.size() - 1);
        ++moved;
    }
}

void ImageListModel::duplicateItems(const QModelIndexList& indexes)
{
    QList<int> rows;
    for (const QModelIndex& idx : indexes) {
        if (idx.isValid())
            rows.append(idx.row());
    }
    std::sort(rows.begin(), rows.end());
    int inserted = 0;
    for (int row : rows) {
        int insertPos = row + 1 + inserted;
        if (insertPos < 0 || insertPos > m_items.size())
            continue;
        const ImageItem& src = m_items.at(row + inserted);
        beginInsertRows(QModelIndex(), insertPos, insertPos);
        ImageItem copy(src.filePath(), false);
        copy.setHidden(src.isHidden());
        copy.setInfo(ImageLoader::loadInfo(src.filePath()));
        // 复制旋转/翻转状态（必须在 setThumbnail 之前：setRotation/setFlipped*
        // 会清空缩略图缓存）。src.thumbnail() 返回的是已应用变换的图，若不复制
        // 状态，副本会显示旋转后的缩略图、但 rotation()/flipped*() 报告未变换，
        // 而且缓存一旦失效外观会突然变化。
        copy.setRotation(src.rotation());
        copy.setFlippedHorizontal(src.flippedHorizontal());
        copy.setFlippedVertical(src.flippedVertical());
        if (src.hasThumbnail(m_thumbnailSize))
            copy.copyThumbnailFrom(src, m_thumbnailSize);
        m_items.insert(insertPos, copy);
        endInsertRows();
        ++inserted;
    }
    if (inserted > 0)
        emit countChanged(m_items.size());
}

void ImageListModel::reloadItem(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    m_items[row].setLoadState(ImageItem::LoadState::Loading);
    m_items[row].setThumbnail(m_thumbnailSize, QPixmap());
    emit dataChanged(index(row), index(row), {LoadStateRole, ThumbnailRole});
    loadImageDataAsync({row});
}

void ImageListModel::loadVisibleRange(int firstRow, int lastRow, int buffer)
{
    if (m_items.isEmpty())
        return;
    int first = qMax(0, firstRow - buffer);
    int last = qMin(m_items.size() - 1, lastRow + buffer);
    QList<int> rows;
    for (int i = first; i <= last; ++i) {
        if (!m_items.at(i).isValid() &&
            m_items.at(i).loadState() == ImageItem::LoadState::Loading &&
            !m_imageDataQueue.contains(i)) {
            rows.append(i);
        }
    }
    loadImageDataAsync(rows);
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
    return &m_items.at(row);
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
    if (fromRow < 0 || fromRow >= m_items.size() || toRow < 0 || toRow >= m_items.size() ||
        fromRow == toRow)
        return;

    if (!beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(),
                       toRow > fromRow ? toRow + 1 : toRow))
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
    emit dataChanged(index(row), index(row), {RotationRole, ThumbnailRole});
}

void ImageListModel::flipHorizontalItem(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    m_items[row].setFlippedHorizontal(!m_items[row].flippedHorizontal());
    emit dataChanged(index(row), index(row), {ThumbnailRole});
}

void ImageListModel::flipVerticalItem(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    m_items[row].setFlippedVertical(!m_items[row].flippedVertical());
    emit dataChanged(index(row), index(row), {ThumbnailRole});
}

QStringList ImageListModel::filePaths() const
{
    return visibleFilePaths();
}

QStringList ImageListModel::allFilePaths() const
{
    QStringList paths;
    for (const auto& item : m_items)
        paths.append(item.filePath());
    return paths;
}

QStringList ImageListModel::visibleFilePaths() const
{
    QStringList paths;
    for (const auto& item : m_items) {
        if (!item.isHidden())
            paths.append(item.filePath());
    }
    return paths;
}

QStringList ImageListModel::selectedFilePaths() const
{
    QStringList paths;
    for (const auto& item : m_items) {
        if (item.isSelected() && !item.isHidden())
            paths.append(item.filePath());
    }
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

int ImageListModel::visibleCount() const
{
    int count = 0;
    for (const auto& item : m_items) {
        if (!item.isHidden())
            ++count;
    }
    return count;
}

int ImageListModel::hiddenCount() const
{
    int count = 0;
    for (const auto& item : m_items) {
        if (item.isHidden())
            ++count;
    }
    return count;
}

static ImageLoadResult loadImageDataTask(const ImageDataTask& task)
{
    ImageLoadResult result;
    result.row = task.row;
    result.id = task.id;
    // 用 loadInfoQuiet：批量加载时逐张上报会把日志与提示刷满，
    // 失败项由 ImageListModel 汇总后一次性上报。
    result.info = ImageLoader::loadInfoQuiet(task.path);
    if (result.info.valid) {
        // 用 loadPreview（返回 QImage）而不是 loadThumbnail（返回 QPixmap）：
        // 本函数运行在 QtConcurrent 工作线程中，QPixmap 不允许在非 GUI 线程构造。
        result.thumbnail =
            ImageLoader::loadPreview(task.path, QSize(task.thumbnailSize, task.thumbnailSize));
    }
    return result;
}

void ImageListModel::loadImageDataAsync(const QList<int>& rows)
{
    if (rows.isEmpty())
        return;

    // 去重合并到队列
    for (int row : rows) {
        if (row >= 0 && row < m_items.size() && !m_items.at(row).isValid() &&
            m_items.at(row).loadState() == ImageItem::LoadState::Loading &&
            !m_imageDataQueue.contains(row)) {
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
        // 队列已空且当前批次的结果都已投递（resultReadyAt 先于 finished 触发），
        // 此时汇总上报本批次的读取失败项。
        // 注意时机：finished 回调里 m_pendingImageDataTasks 仍持有刚完成的那一批，
        // 不能拿它是否为空来判断「批次结束」。
        reportCollectedLoadFailures();

        if (m_pendingImageDataTasks.isEmpty()) {
            m_imageDataLoadTotal = 0;
            m_imageDataLoadDone = 0;
            emit thumbnailLoadFinished();
        }
        return;
    }

    m_pendingImageDataTasks.clear();
    for (int row : m_imageDataQueue) {
        if (row >= 0 && row < m_items.size() && !m_items.at(row).isValid()) {
            ImageDataTask task;
            task.row = row;
            task.id = m_items.at(row).id();
            task.path = m_items.at(row).filePath();
            task.thumbnailSize = m_thumbnailSize;
            m_pendingImageDataTasks.append(task);
        }
    }
    m_imageDataQueue.clear();

    if (m_pendingImageDataTasks.isEmpty()) {
        reportCollectedLoadFailures();
        emit thumbnailLoadFinished();
        return;
    }

    if (m_imageDataLoadTotal == 0)
        m_imageDataLoadTotal = m_pendingImageDataTasks.size();
    m_imageDataLoadDone = 0;
    emit thumbnailLoadStarted(m_imageDataLoadTotal);

    QFuture<ImageLoadResult> future =
        QtConcurrent::mapped(m_pendingImageDataTasks, loadImageDataTask);
    m_imageDataWatcher->setFuture(future);
}

void ImageListModel::onImageDataLoaded(int index)
{
    if (index < 0 || index >= m_pendingImageDataTasks.size())
        return;

    ImageLoadResult result = m_imageDataWatcher->resultAt(index);

    // 按 id 而非行号定位：加载期间用户可能删除/重排图片，task 里记录的 row
    // 会失效，直接按 row 回写会把缩略图/信息写到错误的图片上。
    int row = -1;
    if (!result.id.isEmpty()) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).id() == result.id) {
                row = i;
                break;
            }
        }
    } else if (result.row >= 0 && result.row < m_items.size()) {
        row = result.row;
    }
    if (row < 0)
        return; // 对应图片已被移除

    ImageItem& item = m_items[row];
    // 先写缩略图再写 info：setInfo 会把条目标记为「有效」，
    // 反过来则会出现「已有效但还没有缩略图」的中间态，
    // 那一瞬间界面会闪出一个占位符。
    // 用 setDecodedThumbnail：它会把旋转/翻转应用到解码结果上再入缓存，
    // 与 ImageItem::thumbnail() 的缓存语义一致（直接存原始 pixmap 会导致
    // 已旋转/翻转的图片显示未变换的缩略图）。
    item.setDecodedThumbnail(m_thumbnailSize, result.thumbnail);
    item.setInfo(result.info);
    // 记录读取失败的项，待本轮加载结束时汇总上报（避免批量失败刷屏）。
    if (!result.info.valid) {
        const QString& path = item.filePath();
        if (!path.isEmpty() && !m_loadFailurePaths.contains(path))
            m_loadFailurePaths.append(path);
    }

    emit dataChanged(this->index(row), this->index(row),
                     {WidthRole, HeightRole, FileSizeRole, FormatRole, ValidRole, ThumbnailRole,
                      ResolutionRole, LoadStateRole});

    ++m_imageDataLoadDone;
    emit thumbnailLoadProgress(m_imageDataLoadDone, m_imageDataLoadTotal);
}

void ImageListModel::reportCollectedLoadFailures()
{
    if (m_loadFailurePaths.isEmpty())
        return;

    // 一次失败上报覆盖整批：既保留诊断价值（能看出哪些文件坏了），
    // 又不会因为拖入几百张坏图就把日志和提示刷满。
    const int count = m_loadFailurePaths.size();
    constexpr int kMaxListed = 5;
    QString detail;
    if (count <= kMaxListed) {
        detail = m_loadFailurePaths.join(QStringLiteral("; "));
    } else {
        detail = m_loadFailurePaths.mid(0, kMaxListed).join(QStringLiteral("; ")) +
                 QStringLiteral(" … (+%1)").arg(count - kMaxListed);
    }
    ErrorHandler::instance().report(
        ErrorCode::FILE_CORRUPTED,
        QStringLiteral("%1 image(s) failed to load: %2").arg(count).arg(detail));

    m_loadFailurePaths.clear();
}

} // namespace yingtu
