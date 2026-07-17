#include "ImageListWidget.h"
#include "core/ExportManager.h"
#include "utils/FileUtils.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QRubberBand>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace yingtu {

namespace {

constexpr int ItemSpacing = 4;
constexpr int ToolTipDelayMs = 200;
constexpr int LoadingIntervalMs = 50;

QRect failedRetryRect(const QRect& thumbRect)
{
    const int w = 48;
    const int h = 20;
    return QRect(thumbRect.center().x() - w / 2,
                 thumbRect.bottom() - h - 6,
                 w, h);
}

QPoint lockIconPos(const QRect& thumbRect)
{
    return QPoint(thumbRect.right() - 16, thumbRect.top() + 2);
}

} // anonymous namespace

class ImageItemDelegate : public QStyledItemDelegate
{
public:
    explicit ImageItemDelegate(ImageListWidget* widget, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_widget(widget)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        const QRect cellRect = opt.rect;
        const QRect thumbRect = m_widget ? m_widget->thumbnailRect(cellRect)
                                         : QRect(cellRect.center() - QPoint(96, 26), QSize(192, 52));
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        const bool hidden = index.data(ImageListModel::HiddenRole).toBool();
        const auto loadState = static_cast<ImageItem::LoadState>(index.data(ImageListModel::LoadStateRole).toInt());

        painter->save();

        // 背景：悬停 / 选中
        if (selected) {
            painter->fillRect(cellRect, QColor(30, 144, 255, 40));
        } else if (hover) {
            painter->fillRect(cellRect, QColor(128, 128, 128, 30));
        }

        // 隐藏项整体半透明
        if (hidden)
            painter->setOpacity(0.5);

        // 缩略图区域
        if (loadState == ImageItem::LoadState::Loading) {
            painter->fillRect(thumbRect, QColor(128, 128, 128, 128));
            drawSpinner(painter, thumbRect);
        } else if (loadState == ImageItem::LoadState::Failed) {
            painter->fillRect(thumbRect, QColor(255, 200, 200, 180));
            painter->setPen(QColor(200, 50, 50));
            painter->drawRect(thumbRect);
            painter->setPen(QColor(180, 0, 0));
            const QString failedText = m_widget ? m_widget->tr("加载失败") : QStringLiteral("加载失败");
            painter->drawText(thumbRect, Qt::AlignCenter | Qt::TextWordWrap,
                              QStringLiteral("⚠\n%1").arg(failedText));
            // 重试按钮（与鼠标命中区域一致）
            QRect retryRect = failedRetryRect(thumbRect);
            painter->setBrush(QColor(255, 255, 255));
            painter->drawRect(retryRect);
            painter->setPen(QColor(180, 0, 0));
            const QString retryText = m_widget ? m_widget->tr("重试") : QStringLiteral("重试");
            painter->drawText(retryRect, Qt::AlignCenter, retryText);
        } else {
            QPixmap thumb = index.data(ImageListModel::ThumbnailRole).value<QPixmap>();
            if (!thumb.isNull()) {
                QPixmap scaled = thumb.scaled(thumbRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                QRect srcRect(QPoint(), thumbRect.size());
                srcRect.moveCenter(scaled.rect().center());
                painter->drawPixmap(thumbRect, scaled, srcRect);
            } else {
                painter->setPen(QColor(150, 150, 150));
                painter->drawRect(thumbRect);
                painter->drawText(thumbRect, Qt::AlignCenter, QStringLiteral("?"));
            }
        }

        Q_UNUSED(index)

        painter->restore();

        // 隐藏标记 🔒（不透明）
        if (hidden) {
            painter->setPen(opt.palette.text().color());
            painter->drawText(lockIconPos(thumbRect), QStringLiteral("🔒"));
        }

        // 选中边框
        if (selected) {
            painter->setPen(QPen(QColor(30, 144, 255), 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(cellRect.adjusted(1, 1, -1, -1));
        }
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option)
        Q_UNUSED(index)
        return m_widget ? m_widget->cellSize() : QSize(200, 60);
    }

private:
    void drawSpinner(QPainter* painter, const QRect& rect) const
    {
        if (!m_widget)
            return;
        const int size = qMin(rect.width(), rect.height()) / 3;
        const QRect r(rect.center().x() - size / 2,
                      rect.center().y() - size / 2,
                      size, size);
        painter->setPen(QPen(QColor(30, 144, 255), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawArc(r, m_widget->loadingAngle() * 16, 270 * 16);
    }

    ImageListWidget* m_widget = nullptr;
};

class ImageListView : public QListView
{
    Q_OBJECT
public:
    explicit ImageListView(ImageListWidget* widget, QWidget* parent = nullptr)
        : QListView(parent)
        , m_widget(widget)
        , m_model(widget ? widget->model() : nullptr)
    {
        setMouseTracking(true);
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setViewMode(QListView::IconMode);
        if (widget)
            setGridSize(widget->cellSize());
        else
            setGridSize(QSize(200, 60));
        setSpacing(ItemSpacing);
        setResizeMode(QListView::Adjust);
        setUniformItemSizes(true);
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setContextMenuPolicy(Qt::CustomContextMenu);
        setFrameStyle(QFrame::NoFrame);
        setViewportMargins(0, 0, 0, 0);

        m_tooltipTimer = new QTimer(this);
        m_tooltipTimer->setSingleShot(true);
        connect(m_tooltipTimer, &QTimer::timeout, this, &ImageListView::showPendingTooltip);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            if (m_inRubberBand) {
                endRubberBand();
                event->accept();
                return;
            }
            selectionModel()->clear();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) {
            selectAll();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Delete) {
            QModelIndexList sel = selectionModel()->selectedIndexes();
            if (!sel.isEmpty() && m_model) {
                m_model->removeImages(sel);
                event->accept();
                return;
            }
        }
        QListView::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QListView::mousePressEvent(event);
            return;
        }

        hideTooltip();
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid()) {
            const auto loadState = static_cast<ImageItem::LoadState>(idx.data(ImageListModel::LoadStateRole).toInt());
            if (loadState == ImageItem::LoadState::Failed && failedRetryRect(visualRect(idx)).contains(event->pos())) {
                if (m_model)
                    m_model->reloadItem(idx.row());
                event->accept();
                return;
            }
            QListView::mousePressEvent(event);
            return;
        }

        // 空白处：开始拖框
        setFocus();
        if (!m_rubberBand)
            m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
        m_rubberBandOrigin = event->pos();
        m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, QSize()));
        m_rubberBand->show();
        m_inRubberBand = true;
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_inRubberBand) {
            m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, event->pos()).normalized());
            event->accept();
            return;
        }

        startTooltipTimer(indexAt(event->pos()));
        QListView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_inRubberBand) {
            applyRubberBandSelection(event);
            endRubberBand();
            event->accept();
            return;
        }
        QListView::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        hideTooltip();
        QListView::leaveEvent(event);
    }

private:
    void startTooltipTimer(const QModelIndex& index)
    {
        m_tooltipIndex = index;
        if (index.isValid()) {
            m_tooltipTimer->start(ToolTipDelayMs);
        } else {
            m_tooltipTimer->stop();
        }
    }

    void showPendingTooltip()
    {
        const QPoint cursorPos = QCursor::pos();
        const QPoint viewportPos = viewport()->mapFromGlobal(cursorPos);
        const QModelIndex idx = indexAt(viewportPos);
        if (!idx.isValid() || idx != m_tooltipIndex)
            return;
        const QString text = idx.data(Qt::ToolTipRole).toString();
        if (!text.isEmpty()) {
            QToolTip::showText(cursorPos, text, viewport(), visualRect(idx));
        }
    }

    void hideTooltip()
    {
        m_tooltipTimer->stop();
        QToolTip::hideText();
    }

    void applyRubberBandSelection(QMouseEvent* event)
    {
        if (!m_model)
            return;
        const QRect band = QRect(m_rubberBandOrigin, event->pos()).normalized();
        const bool ctrl = event->modifiers() & Qt::ControlModifier;
        QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::Rows | QItemSelectionModel::Select;

        // 收集满足 50% 面积相交的项
        QModelIndexList toSelect;
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const QModelIndex idx = m_model->index(row);
            const QRect itemRect = visualRect(idx);
            const QRect inter = band & itemRect;
            if (inter.isEmpty())
                continue;
            const int interArea = inter.width() * inter.height();
            const int itemArea = itemRect.width() * itemRect.height();
            if (itemArea > 0 && interArea * 2 >= itemArea)
                toSelect.append(idx);
        }

        if (!ctrl)
            selectionModel()->clear();
        for (const QModelIndex& idx : toSelect)
            selectionModel()->select(idx, flags);
    }

    void endRubberBand()
    {
        if (m_rubberBand) {
            m_rubberBand->hide();
            delete m_rubberBand;
            m_rubberBand = nullptr;
        }
        m_inRubberBand = false;
    }

    ImageListWidget* m_widget = nullptr;
    ImageListModel* m_model = nullptr;
    QTimer* m_tooltipTimer = nullptr;
    QModelIndex m_tooltipIndex;
    QRubberBand* m_rubberBand = nullptr;
    QPoint m_rubberBandOrigin;
    bool m_inRubberBand = false;
};

ImageListWidget::ImageListWidget(ImageListModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_emptyLabel = new QLabel(tr("拖拽图片到此处或点击添加图片"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #909399;"));
    m_emptyLabel->setVisible(false);

    m_view = new ImageListView(this, this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(new ImageItemDelegate(this, m_view));

    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid())
            emit imageDoubleClicked(index.row());
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection& selected, const QItemSelection& deselected) {
                if (m_model) {
                    for (const QModelIndex& idx : deselected.indexes())
                        m_model->setData(idx, false, ImageListModel::SelectedRole);
                    for (const QModelIndex& idx : selected.indexes())
                        m_model->setData(idx, true, ImageListModel::SelectedRole);
                }
                QModelIndexList indexes = selected.indexes();
                if (!indexes.isEmpty())
                    emit imageSelected(indexes.first().row());
                emit imageSelectionChanged();
            });
    connect(m_view, &QListView::customContextMenuRequested, this, &ImageListWidget::showContextMenu);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &ImageListWidget::onScrollOrResize);

    if (m_model) {
        connect(m_model, &ImageListModel::countChanged, this, &ImageListWidget::onModelCountChanged);
        connect(m_model, &ImageListModel::modelReset, this, &ImageListWidget::loadVisibleRange);
        connect(m_model, &ImageListModel::rowsInserted, this, &ImageListWidget::loadVisibleRange);
        connect(m_model, &ImageListModel::rowsMoved, this, &ImageListWidget::loadVisibleRange);
    }

    m_loadingTimer = new QTimer(this);
    connect(m_loadingTimer, &QTimer::timeout, this, &ImageListWidget::onLoadingTick);
    m_loadingTimer->start(LoadingIntervalMs);

    layout->addWidget(m_view);
    layout->addWidget(m_emptyLabel);
    setLayout(layout);

    updateEmptyState();
    setAcceptDrops(true);

    if (m_view->viewport())
        m_view->viewport()->installEventFilter(this);
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
    if (!paths.isEmpty() && m_model)
        m_model->addImages(paths);
    event->acceptProposedAction();
}

void ImageListWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePanelWidth();
}

void ImageListWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (QWidget* w = window()) {
        if (!m_windowFilterInstalled) {
            w->installEventFilter(this);
            m_windowFilterInstalled = true;
        }
    }
    updatePanelWidth();
    loadVisibleRange();
}

bool ImageListWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window() && event->type() == QEvent::Resize) {
        updatePanelWidth();
    } else if (m_view && watched == m_view->viewport() && event->type() == QEvent::Resize) {
        onScrollOrResize();
    }
    return QWidget::eventFilter(watched, event);
}

void ImageListWidget::updatePanelWidth()
{
    QWidget* top = window();
    const int windowWidth = top ? top->width() : width();
    int target = 200;
    if (windowWidth >= 1920)
        target = 260;
    else if (windowWidth >= 1280)
        target = 220;
    else if (windowWidth >= 800)
        target = 200;
    setMinimumWidth(180);
    setMaximumWidth(280);
    setFixedWidth(target);

    int viewportW = target;
    if (m_view && m_view->viewport())
        viewportW = m_view->viewport()->width();
    updateGridMetrics(viewportW);
}

void ImageListWidget::updateGridMetrics(int panelWidth)
{
    constexpr int HorizontalMargin = 4;
    constexpr int VerticalMargin = 4;
    constexpr int MinThumb = 52;

    m_cellWidth = panelWidth;
    m_thumbSize = qMax(MinThumb, panelWidth - 2 * HorizontalMargin);
    m_cellHeight = m_thumbSize + 2 * VerticalMargin;
    m_thumbMarginH = (m_cellWidth - m_thumbSize) / 2;
    m_topMargin = (m_cellHeight - m_thumbSize) / 2;
    if (m_view)
        m_view->setGridSize(QSize(m_cellWidth, m_cellHeight));
}

QRect ImageListWidget::thumbnailRect(const QRect& cellRect) const
{
    return QRect(cellRect.left() + m_thumbMarginH,
                 cellRect.top() + m_topMargin,
                 m_thumbSize,
                 m_thumbSize);
}

void ImageListWidget::loadVisibleRange()
{
    if (!m_model || !m_view)
        return;
    const QRect viewportRect = m_view->viewport()->rect();
    const QModelIndex topLeft = m_view->indexAt(viewportRect.topLeft());
    const QModelIndex bottomRight = m_view->indexAt(viewportRect.bottomRight());
    int first = topLeft.isValid() ? topLeft.row() : 0;
    int last = bottomRight.isValid() ? bottomRight.row() : m_model->rowCount() - 1;
    if (last < first)
        last = m_model->rowCount() - 1;
    m_model->loadVisibleRange(first, last);
}

void ImageListWidget::onScrollOrResize()
{
    loadVisibleRange();
}

void ImageListWidget::onModelCountChanged(int count)
{
    Q_UNUSED(count)
    updateEmptyState();
    loadVisibleRange();
}

void ImageListWidget::onLoadingTick()
{
    m_loadingAngle = (m_loadingAngle + 30) % 360;
    if (m_view && m_view->viewport())
        m_view->viewport()->update();
}

void ImageListWidget::showContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    const QModelIndex index = m_view->indexAt(pos);
    buildMenu(menu, index);
    if (!menu.isEmpty())
        menu.exec(m_view->mapToGlobal(pos));
}

void ImageListWidget::buildMenu(QMenu& menu, const QModelIndex& index)
{
    if (!m_model)
        return;

    QModelIndexList selected = m_view->selectionModel()->selectedIndexes();
    if (selected.isEmpty() && index.isValid())
        selected.append(index);

    if (!selected.isEmpty()) {
        const bool single = selected.size() == 1;
        Q_UNUSED(single)

        // 显示/隐藏
        bool allHidden = std::all_of(selected.begin(), selected.end(),
                                     [](const QModelIndex& idx) { return idx.data(ImageListModel::HiddenRole).toBool(); });
        menu.addAction(allHidden ? tr("显示") : tr("隐藏"), this, [this, selected, allHidden]() {
            m_model->setHiddenForRows(selected, !allHidden);
        });
        menu.addSeparator();

        menu.addAction(tr("复制"), this, [this, selected]() {
            m_model->duplicateItems(selected);
        });
        menu.addAction(tr("复制到剪贴板"), this, [selected]() {
            QStringList paths;
            for (const QModelIndex& idx : selected)
                paths.append(idx.data(ImageListModel::FilePathRole).toString());
            QGuiApplication::clipboard()->setText(paths.join(QStringLiteral("\n")));
        });
        menu.addSeparator();

        menu.addAction(tr("移到最前"), this, [this, selected]() { m_model->moveToFront(selected); });
        menu.addAction(tr("移到最后"), this, [this, selected]() { m_model->moveToBack(selected); });
        menu.addSeparator();

        if (single) {
            QString filePath = selected.first().data(ImageListModel::FilePathRole).toString();
            menu.addAction(tr("在资源管理器中显示"), this, [filePath]() {
                if (!filePath.isEmpty())
                    ExportManager::showInFolder(filePath);
            });
            menu.addAction(tr("快速查看"), this, [filePath]() {
                if (!filePath.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
            });
            menu.addSeparator();
        } else {
            QString firstPath = selected.first().data(ImageListModel::FilePathRole).toString();
            menu.addAction(tr("在资源管理器中显示"), this, [firstPath]() {
                if (!firstPath.isEmpty())
                    ExportManager::showInFolder(firstPath);
            });
            menu.addAction(tr("快速查看"), this, [firstPath]() {
                if (!firstPath.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(firstPath));
            });
            menu.addSeparator();
        }

        menu.addAction(tr("移除"), this, [this, selected]() { m_model->removeImages(selected); });
    } else {
        menu.addAction(tr("添加图片"), this, [this]() { emit addImagesRequested(); });
        menu.addAction(tr("全选"), this, [this]() { m_view->selectAll(); });
        if (m_model->rowCount() > 0) {
            menu.addSeparator();
            menu.addAction(tr("清空列表"), this, [this]() { emit clearRequested(); });
        }
    }
}

void ImageListWidget::setupView()
{
    // 所有视图初始化已放在构造函数中
}

void ImageListWidget::updateEmptyState()
{
    if (!m_model || !m_view)
        return;
    bool empty = m_model->rowCount() == 0;
    m_view->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
}

} // namespace yingtu

#include "ImageListWidget.moc"
