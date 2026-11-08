#include "ImageListModel.h"
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/ImageLoader.h"
#include "utils/ImageTaskPools.h"
#include <QDataStream>
#include <QFileInfo>
#include <QMutexLocker>
#include <QMimeData>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>
#include <algorithm>

namespace yingtu {

namespace {

// 单个缩略图解码任务（工作共享：worker 从模型队列拉活直到排空）。
class ThumbnailWorker : public QRunnable
{
public:
    explicit ThumbnailWorker(ImageListModel* model) : m_model(model) { setAutoDelete(true); }
    void run() override
    {
        if (auto* model = m_model.data())
            model->drainThumbnailQueue();
    }

private:
    QPointer<ImageListModel> m_model;
};

} // namespace

ImageListModel::ImageListModel(QObject* parent) : QAbstractListModel(parent)
{
    // 缩略图尺寸与列表单元格实际尺寸匹配（见 ImageDataTask::thumbnailSize 的说明）。
    // 原先在此硬编码 120，会让每张缩略图在绘制时被放大、列表明显模糊。
    m_thumbnailSize = 240;
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
        // 逐行读出全部被拖拽的行：此前只读第一行，多选拖动只会移动一张。
        struct DragRow
        {
            int fromRow = -1;
            QString id;
        };
        QList<DragRow> rows;
        while (!stream.atEnd()) {
            DragRow dr;
            stream >> dr.fromRow >> dr.id;
            if (dr.fromRow < 0 || dr.fromRow >= m_items.size() ||
                m_items.at(dr.fromRow).id() != dr.id)
                return false;
            rows.append(dr);
        }
        if (rows.isEmpty())
            return false;

        int toRow = row;
        if (toRow < 0 || toRow > m_items.size())
            toRow = m_items.size();

        if (rows.size() == 1) {
            const int fromRow = rows.first().fromRow;
            if (toRow == fromRow || toRow == fromRow + 1)
                return false;
            moveItem(fromRow, toRow > fromRow ? toRow - 1 : toRow);
            return true;
        }

        // 多选：按列表内的现有顺序整体搬到目标位置。
        // 逐条 moveItem 会因行号随移动漂移而出错，直接重排一次。
        QList<int> currentRows;
        for (const DragRow& dr : rows) {
            for (int i = 0; i < m_items.size(); ++i) {
                if (m_items.at(i).id() == dr.id) {
                    currentRows.append(i);
                    break;
                }
            }
        }
        std::sort(currentRows.begin(), currentRows.end());

        beginResetModel();
        QList<ImageItem> moved;
        for (int r : currentRows)
            moved.append(m_items.at(r));
        for (int i = currentRows.size() - 1; i >= 0; --i)
            m_items.removeAt(currentRows.at(i));
        int adjustedTo = toRow;
        for (int r : currentRows) {
            if (r < toRow)
                --adjustedTo;
        }
        adjustedTo = qBound(0, adjustedTo, m_items.size());
        for (int i = 0; i < moved.size(); ++i)
            m_items.insert(qMin(adjustedTo + i, m_items.size()), moved.at(i));
        endResetModel();
        return true;
    }

    // 外部文件拖入
    if (data->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : data->urls())
            paths.append(url.toLocalFile());
        paths = FileUtils::expandToImageFiles(paths);
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
        loadImageDataAsync({row}, false);
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
        loadImageDataAsync(newRows, false);
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
    {
        QMutexLocker lock(&m_queueMutex);
        m_imageDataQueue.clear();
        m_queuedIds.clear();
        m_wantedIds.clear();
    }
    m_loadFailurePaths.clear();
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged(0);
    emit selectionChanged();
    // 在途 worker 会发现队列已空而退出，其结果因 id 找不到行而被丢弃，
    // 会话随后由 onThumbnailWorkerExited 正常收尾。
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
    loadImageDataAsync({row}, false);
}

void ImageListModel::loadVisibleRange(int firstRow, int lastRow, int buffer)
{
    if (m_items.isEmpty())
        return;
    int first = qMax(0, firstRow - buffer);
    int last = qMin(m_items.size() - 1, lastRow + buffer);
    QList<int> rows;
    {
        QMutexLocker lock(&m_queueMutex);
        // 整体重算「想要集合」：只有当前可视窗口（+buffer）内的待加载图片
        // 才值得解码。快速滚动时，上一窗口排队的任务开工前会发现 id 不在
        // 集合内而跳过 —— 滚过 100 行不再产生 100 次无效解码。
        m_wantedIds.clear();
        for (int i = first; i <= last; ++i) {
            if (!m_items.at(i).isValid() &&
                m_items.at(i).loadState() == ImageItem::LoadState::Loading) {
                m_wantedIds.insert(m_items.at(i).id());
                rows.append(i);
            }
        }
    }
    loadImageDataAsync(rows, true);
}

void ImageListModel::sortByField(SortField field, Qt::SortOrder order)
{
    if (m_items.size() < 2)
        return;

    // 先取 key 再排序：日期字段需要现场 stat 文件，排序中不做磁盘 IO。
    const int n = m_items.size();
    QVector<qint64> numKeys(n, 0);
    QVector<QString> strKeys(n);
    for (int i = 0; i < n; ++i) {
        const ImageItem& item = m_items.at(i);
        switch (field) {
        case SortField::Name:
            strKeys[i] = item.displayName().toLower();
            break;
        case SortField::FileSize:
            numKeys[i] = item.fileSize();
            break;
        case SortField::Date:
            numKeys[i] = QFileInfo(item.filePath()).lastModified().toMSecsSinceEpoch();
            break;
        case SortField::Dimensions:
            numKeys[i] = qint64(item.width()) * qint64(item.height());
            break;
        }
    }

    QVector<int> order_(n);
    for (int i = 0; i < n; ++i)
        order_[i] = i;
    const bool ascending = (order == Qt::AscendingOrder);
    std::stable_sort(order_.begin(), order_.end(),
                     [field, ascending, &numKeys, &strKeys](int a, int b) {
                         if (field == SortField::Name)
                             return ascending ? strKeys[a] < strKeys[b] : strKeys[a] > strKeys[b];
                         return ascending ? numKeys[a] < numKeys[b] : numKeys[a] > numKeys[b];
                     });

    beginResetModel();
    QList<ImageItem> sorted;
    sorted.reserve(n);
    for (int idx : order_)
        sorted.append(m_items.at(idx));
    m_items = sorted;
    endResetModel();
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

void ImageListModel::loadImageDataAsync(const QList<int>& rows, bool cancelable)
{
    if (rows.isEmpty())
        return;

    const bool sessionWasIdle = (m_imageDataLoadTotal == 0);
    int added = 0;
    {
        QMutexLocker lock(&m_queueMutex);
        for (int row : rows) {
            if (row < 0 || row >= m_items.size() || m_items.at(row).isValid() ||
                m_items.at(row).loadState() != ImageItem::LoadState::Loading)
                continue;
            const QString id = m_items.at(row).id();
            if (m_queuedIds.contains(id))
                continue;
            ImageDataTask task;
            task.row = row;
            task.id = id;
            task.path = m_items.at(row).filePath();
            task.thumbnailSize = m_thumbnailSize;
            task.cancelable = cancelable;
            m_imageDataQueue.append(task);
            m_queuedIds.insert(id);
            if (cancelable)
                m_wantedIds.insert(id);
            ++added;
        }
    }
    if (added == 0)
        return;

    // total 在会话内累积：滚动期间新入队的任务也计入进度分母，
    // 进度条随每次 progress(done,total) 自适应。
    m_imageDataLoadTotal += added;
    if (sessionWasIdle)
        emit thumbnailLoadStarted(m_imageDataLoadTotal);

    pumpThumbnailQueue();
}

void ImageListModel::pumpThumbnailQueue()
{
    QMutexLocker lock(&m_queueMutex);
    const int maxWorkers = ImageTaskPools::thumbnailPool()->maxThreadCount();
    const int want = qMin(m_imageDataQueue.size(), maxWorkers - m_activeWorkers);
    for (int i = 0; i < want; ++i) {
        ++m_activeWorkers;
        ImageTaskPools::thumbnailPool()->start(new ThumbnailWorker(this));
    }
}

void ImageListModel::drainThumbnailQueue()
{
    // 工作共享：每个 worker 循环从队列拉活，直到排空。
    // 并发度由 thumbnailPool 的 maxThreadCount 限制，与 libvips 内部线程
    // 相加后总量受控（此前是 N 个 QtConcurrent worker × N/2 个 vips 线程）。
    for (;;) {
        ImageDataTask task;
        bool decode = false;
        {
            QMutexLocker lock(&m_queueMutex);
            if (m_imageDataQueue.isEmpty()) {
                --m_activeWorkers;
                QMetaObject::invokeMethod(this, &ImageListModel::onThumbnailWorkerExited,
                                          Qt::QueuedConnection);
                return;
            }
            task = m_imageDataQueue.takeFirst();
            m_queuedIds.remove(task.id);
            // 想要集合作废：可视范围懒加载任务已滚出窗口 → 跳过解码。
            decode = !task.cancelable || m_wantedIds.contains(task.id);
            if (decode)
                ++m_inFlight;
        }

        if (decode) {
            const ImageLoadResult result = loadImageDataTask(task);
            QMetaObject::invokeMethod(
                this,
                [this, result]() { onThumbnailTaskDone(result); },
                Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, &ImageListModel::onThumbnailTaskSkipped,
                                      Qt::QueuedConnection);
        }
    }
}

void ImageListModel::onThumbnailTaskDone(const ImageLoadResult& result)
{
    {
        QMutexLocker lock(&m_queueMutex);
        --m_inFlight;
    }

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

    if (row >= 0) {
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
                         {WidthRole, HeightRole, FileSizeRole, FormatRole, ValidRole,
                          ThumbnailRole, ResolutionRole, LoadStateRole});
    }
    // row < 0：对应图片已被移除 —— 结果丢弃（跳过也计入进度，进度条能走完）。
    onThumbnailTaskSettled();
}

void ImageListModel::onThumbnailTaskSkipped()
{
    onThumbnailTaskSettled();
}

void ImageListModel::onThumbnailWorkerExited()
{
    onThumbnailTaskSettled();
}

void ImageListModel::onThumbnailTaskSettled()
{
    ++m_imageDataLoadDone;
    emit thumbnailLoadProgress(m_imageDataLoadDone, m_imageDataLoadTotal);
    settleThumbnailSession();
}

void ImageListModel::settleThumbnailSession()
{
    bool finished = false;
    {
        QMutexLocker lock(&m_queueMutex);
        finished = m_imageDataQueue.isEmpty() && m_inFlight == 0 && m_activeWorkers == 0;
    }
    if (!finished)
        return;

    // 队列已空且所有结果都已投递：汇总上报本批次读取失败项并收尾。
    reportCollectedLoadFailures();
    m_imageDataLoadTotal = 0;
    m_imageDataLoadDone = 0;
    emit thumbnailLoadFinished();
}

bool ImageListModel::hasPendingLoads() const
{
    QMutexLocker lock(&m_queueMutex);
    return !m_imageDataQueue.isEmpty() || m_inFlight > 0 || m_activeWorkers > 0;
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
