#include "ImageListWidget.h"
#include "core/ExportManager.h"
#include "utils/FileUtils.h"
#include <QAction>
#include <QClipboard>
#include <QDesktopServices>
#include <algorithm>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QStyledItemDelegate>
#include <QUrl>

namespace yingtu {

class ImageItemDelegate : public QStyledItemDelegate
{
public:
    explicit ImageItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, opt.palette.highlight());
        } else if (index.data(ImageListModel::SelectedRole).toBool()) {
            painter->fillRect(opt.rect, QColor(30, 144, 255, 40));
        }

        QPixmap thumb = index.data(ImageListModel::ThumbnailRole).value<QPixmap>();
        QRect thumbRect(opt.rect.left() + 4, opt.rect.top() + 4, 52, 52);
        if (!thumb.isNull()) {
            QPixmap scaled = thumb.scaled(thumbRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter->drawPixmap(thumbRect.center() - scaled.rect().center(), scaled);
        } else {
            painter->setPen(Qt::gray);
            painter->drawRect(thumbRect);
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option)
        Q_UNUSED(index)
        return QSize(60, 60);
    }
};

ImageListWidget::ImageListWidget(ImageListModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_emptyLabel = new QLabel(tr("Drag images here or click Add Images"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #909399;"));
    m_emptyLabel->setVisible(false);

    m_view = new QListView(this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(new ImageItemDelegate(m_view));
    m_view->setViewMode(QListView::IconMode);
    m_view->setGridSize(QSize(60, 60));
    m_view->setSpacing(4);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setUniformItemSizes(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(true);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setMouseTracking(true);

    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid())
            emit imageDoubleClicked(index.row());
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection& selected, const QItemSelection& deselected) {
                for (const QModelIndex& idx : deselected.indexes())
                    m_model->setData(idx, false, ImageListModel::SelectedRole);
                QModelIndexList indexes = selected.indexes();
                for (const QModelIndex& idx : indexes)
                    m_model->setData(idx, true, ImageListModel::SelectedRole);
                if (!indexes.isEmpty())
                    emit imageSelected(indexes.first().row());
                emit imageSelectionChanged();
            });
    connect(m_view, &QListView::customContextMenuRequested, this, &ImageListWidget::showContextMenu);

    QAction* deleteAction = new QAction(tr("Delete"), m_view);
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, [this]() {
        QModelIndexList indexes = m_view->selectionModel()->selectedIndexes();
        if (indexes.isEmpty())
            return;
        m_model->removeImages(indexes);
    });
    m_view->addAction(deleteAction);
    m_view->setAttribute(Qt::WA_DeleteOnClose, false);

    layout->addWidget(m_view);
    layout->addWidget(m_emptyLabel);
    setLayout(layout);

    connect(m_model, &ImageListModel::countChanged, this, &ImageListWidget::updateEmptyState);
    updateEmptyState();
    setAcceptDrops(true);
}

void ImageListWidget::setEmptyHint(const QString& hint)
{
    m_emptyLabel->setText(hint);
}

QModelIndexList ImageListWidget::selectedIndexes() const
{
    if (!m_view)
        return QModelIndexList();
    return m_view->selectionModel()->selectedIndexes();
}

void ImageListWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ImageListWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ImageListWidget::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls())
        return;

    QStringList paths;
    for (const QUrl& url : mime->urls()) {
        QString path = url.toLocalFile();
        if (FileUtils::isSupportedImage(path))
            paths.append(path);
    }
    if (!paths.isEmpty())
        m_model->addImages(paths);
    event->acceptProposedAction();
}

void ImageListWidget::showContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    QModelIndex index = m_view->indexAt(pos);
    buildMenu(menu, index);
    if (!menu.isEmpty())
        menu.exec(m_view->mapToGlobal(pos));
}

void ImageListWidget::buildMenu(QMenu& menu, const QModelIndex& index)
{
    QModelIndexList selected = m_view->selectionModel()->selectedIndexes();
    if (selected.isEmpty() && index.isValid())
        selected.append(index);

    if (!selected.isEmpty()) {
        const bool single = selected.size() == 1;
        const int firstRow = selected.first().row();

        if (single) {
            QString filePath = selected.first().data(ImageListModel::FilePathRole).toString();
            menu.addAction(tr("Open File Location"), this, [filePath]() {
                if (!filePath.isEmpty())
                    ExportManager::showInFolder(filePath);
            });
            menu.addAction(tr("Copy File Path"), this, [filePath]() {
                QGuiApplication::clipboard()->setText(filePath);
            }, QKeySequence::Copy);
            menu.addAction(tr("Preview Image"), this, [filePath]() {
                if (!filePath.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
            });
            menu.addSeparator();
        }

        menu.addAction(tr("Rotate"), this, [this, selected]() {
            for (const QModelIndex& idx : selected)
                emit rotateRequested(idx.row());
        });
        menu.addAction(tr("Flip Horizontal"), this, [this, selected]() {
            for (const QModelIndex& idx : selected)
                emit flipHorizontalRequested(idx.row());
        });
        menu.addAction(tr("Flip Vertical"), this, [this, selected]() {
            for (const QModelIndex& idx : selected)
                emit flipVerticalRequested(idx.row());
        });
        menu.addSeparator();
        menu.addAction(tr("Move Up"), this, [this, firstRow]() { emit moveUpRequested(firstRow); });
        menu.addAction(tr("Move Down"), this, [this, firstRow]() { emit moveDownRequested(firstRow); });
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, [this, selected]() { m_model->removeImages(selected); });
    } else {
        menu.addAction(tr("Add Images"), this, [this]() { emit addImagesRequested(); });
        if (m_model->rowCount() > 0) {
            menu.addSeparator();
            menu.addAction(tr("Clear All"), this, [this]() { emit clearRequested(); });
        }
    }
}

void ImageListWidget::setupView()
{
    // 已在构造函数中初始化
}

void ImageListWidget::updateEmptyState()
{
    bool empty = m_model->rowCount() == 0;
    m_view->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
}

} // namespace yingtu
