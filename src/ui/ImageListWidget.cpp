#include "ImageListWidget.h"
#include "app/ImageProApp.h"
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
#include <QHash>
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
    return QRect(thumbRect.center().x() - w / 2, thumbRect.bottom() - h - 6, w, h);
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
        : QStyledItemDelegate(parent), m_widget(widget)
    {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        const QRect cellRect = opt.rect;
        const QRect thumbRect = m_widget
                                    ? m_widget->thumbnailRect(cellRect)
                                    : QRect(cellRect.center() - QPoint(96, 26), QSize(192, 52));
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        const bool hidden = index.data(ImageListModel::HiddenRole).toBool();
        const auto loadState =
            static_cast<ImageItem::LoadState>(index.data(ImageListModel::LoadStateRole).toInt());

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
            const QString failedText =
                m_widget ? m_widget->tr("Load failed") : QStringLiteral("Load failed");
            painter->drawText(thumbRect, Qt::AlignCenter | Qt::TextWordWrap,
                              QStringLiteral("⚠\n%1").arg(failedText));
            // 重试按钮（与鼠标命中区域一致）
            QRect retryRect = failedRetryRect(thumbRect);
            painter->setBrush(QColor(255, 255, 255));
            painter->drawRect(retryRect);
            painter->setPen(QColor(180, 0, 0));
            const QString retryText = m_widget ? m_widget->tr("Retry") : QStringLiteral("重试");
            painter->drawText(retryRect, Qt::AlignCenter, retryText);
        } else {
            QPixmap thumb = index.data(ImageListModel::ThumbnailRole).value<QPixmap>();
            if (!thumb.isNull()) {
                const QPixmap scaled = scaledThumbnail(thumb, thumbRect.size());
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
        const QRect r(rect.center().x() - size / 2, rect.center().y() - size / 2, size, size);
        painter->setPen(QPen(QColor(30, 144, 255), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawArc(r, m_widget->loadingAngle() * 16, 270 * 16);
    }

    /**
     * 取「已缩放」的缩略图，带缓存。
     *
     * 直接调用 scaled() 每次绘制都要对整张缩略图做一次双线性重采样，
     * 滚动列表时每帧对每个可见单元格都重做一遍，开销显著。
     * 所有单元格的目标尺寸相同（由列表的网格度量统一决定），
     * 因此按源图 cacheKey 缓存即可；尺寸变化时整体清空。
     */
    QPixmap scaledThumbnail(const QPixmap& source, const QSize& target) const
    {
        if (m_scaledCacheTarget != target) {
            m_scaledCache.clear();
            m_scaledCacheTarget = target;
        }

        auto it = m_scaledCache.constFind(source.cacheKey());
        if (it != m_scaledCache.constEnd())
            return it.value();

        // 缓存条目上限：滚动过程中会不断遇到新缩略图，
        // 不设上限会让缓存无限增长。
        constexpr int kMaxCachedThumbnails = 64;
        if (m_scaledCache.size() >= kMaxCachedThumbnails)
            m_scaledCache.clear();

        const QPixmap scaled =
            source.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        m_scaledCache.insert(source.cacheKey(), scaled);
        return scaled;
    }

    ImageListWidget* m_widget = nullptr;
    // paint() 是 const 的，缓存必须声明为 mutable。
    // 键用 qint64：Qt 5.15 的 QPixmap::cacheKey() 返回 qint64（Qt 6 才是 CacheKey）。
    mutable QHash<qint64, QPixmap> m_scaledCache;
    mutable QSize m_scaledCacheTarget;
};

class ImageListView : public QListView
{
    Q_OBJECT
public:
    explicit ImageListView(ImageListWidget* widget, QWidget* parent = nullptr)
        : QListView(parent), m_model(widget ? widget->model() : nullptr)
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

    void setStitchToggleMode(bool enabled) { m_stitchToggleMode = enabled; }

signals:
    // Delete 键移除请求。ImageListView 是私有 Q_OBJECT 类，无法直接发出外层
    // ImageListWidget 的信号，因此由外层在构造函数中做信号→信号桥接。
    void removeRequested(const QModelIndexList& indexes);

protected:
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex& index,
                                                         const QEvent* event) const override
    {
        if (m_stitchToggleMode && index.isValid()) {
            // 拼接模式：单击 toggle 选中状态（参与/取消拼接），无需 Ctrl 修饰键。
            return QItemSelectionModel::Toggle | QItemSelectionModel::Rows;
        }
        return QListView::selectionCommand(index, event);
    }
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
                // 走信号而不是直接改 model，让 MainWindow 能修正当前行并刷新预览。
                emit removeRequested(sel);
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
            const auto loadState =
                static_cast<ImageItem::LoadState>(idx.data(ImageListModel::LoadStateRole).toInt());
            if (loadState == ImageItem::LoadState::Failed &&
                failedRetryRect(visualRect(idx)).contains(event->pos())) {
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
        QItemSelectionModel::SelectionFlags flags =
            QItemSelectionModel::Rows | QItemSelectionModel::Select;

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

    ImageListModel* m_model = nullptr;
    QTimer* m_tooltipTimer = nullptr;
    QModelIndex m_tooltipIndex;
    QRubberBand* m_rubberBand = nullptr;
    QPoint m_rubberBandOrigin;
    bool m_inRubberBand = false;
    bool m_stitchToggleMode = false;
};

ImageListWidget::ImageListWidget(ImageListModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_emptyLabel = new QLabel(tr("Drag images here or click Add Images"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #909399;"));
    m_emptyLabel->setVisible(false);

    m_view = new ImageListView(this, this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(new ImageItemDelegate(this, m_view));

    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (m_stitchSelectionMode && index.isValid()) {
            // 双击前的 mousePress 已 toggle 了一次，这里回退以保持原选中状态。
            m_view->selectionModel()->select(index, QItemSelectionModel::Toggle |
                                                        QItemSelectionModel::Rows);
        }
        if (index.isValid())
            emit imageDoubleClicked(index.row());
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection& selected, const QItemSelection& deselected) {
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
    connect(m_view, &QListView::customContextMenuRequested, this,
            &ImageListWidget::showContextMenu);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &ImageListWidget::onScrollOrResize);
    // 桥接内部视图的 Delete 键移除请求到外层信号，使 MainWindow 统一处理
    // （修正 m_currentImageRow 并刷新预览），与右键「移除」走同一条路径。
    // m_view 在头文件中声明为 QListView*（ImageListView 是本文件的私有类，
    // 头文件不可见），因此这里需要显式向下转型才能取出其信号指针。
    connect(static_cast<ImageListView*>(m_view), &ImageListView::removeRequested, this,
            &ImageListWidget::removeRequested);

    if (m_model) {
        connect(m_model, &ImageListModel::countChanged, this,
                &ImageListWidget::onModelCountChanged);
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

void ImageListWidget::setStitchSelectionMode(bool enabled)
{
    m_stitchSelectionMode = enabled;
    auto* view = qobject_cast<ImageListView*>(m_view);
    if (view)
        view->setStitchToggleMode(enabled);
}

void ImageListWidget::selectAllForStitch()
{
    if (!m_view || !m_model)
        return;
    m_view->selectionModel()->clear();
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QModelIndex idx = m_model->index(i);
        if (!m_model->data(idx, ImageListModel::HiddenRole).toBool())
            m_view->selectionModel()->select(idx, QItemSelectionModel::Select |
                                                      QItemSelectionModel::Rows);
    }
}

void ImageListWidget::clearStitchSelection()
{
    if (!m_view || !m_model)
        return;
    m_view->selectionModel()->clear();
}

void ImageListWidget::invertStitchSelection()
{
    if (!m_view || !m_model)
        return;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QModelIndex idx = m_model->index(i);
        if (m_model->data(idx, ImageListModel::HiddenRole).toBool())
            continue;
        // Toggle 每一项的选中状态，实现反选。
        m_view->selectionModel()->select(idx,
                                         QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    }
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

void ImageListWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && !ImageProApp::isLanguageSwitching())
        retranslateUi();
}

void ImageListWidget::retranslateUi()
{
    // 右键菜单每次弹出时现建，其中的 tr() 会自动取当前语言；
    // 委托里的文字同样在绘制时翻译。这里只需处理常驻的空白提示。
    if (m_emptyLabel)
        m_emptyLabel->setText(tr("Drag images here or click Add Images"));
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
        // 视口宽度才是面板的实际可用宽度。分隔条调整面板大小时，
        // 触发的是视口的 resize 而不是窗口的 resize —— 这里必须同步重算
        // 网格度量，否则缩略图会一直停留在早期估算的尺寸（面板里大片留白）。
        updatePanelWidth();
        onScrollOrResize();
    }
    return QWidget::eventFilter(watched, event);
}

void ImageListWidget::updatePanelWidth()
{
    // 面板宽度由 MainWindow 里的 QSplitter 管理（已设置初始 sizes 与 min/max）。
    // 原实现在这里调用 setFixedWidth(target)，会覆盖 min/max，使分隔条完全
    // 无法调整图片列表宽度。现在只根据「实际」宽度更新网格度量。
    int panelW = (m_view && m_view->viewport()) ? m_view->viewport()->width() : width();
    if (panelW <= 0) {
        // 首次布局前 viewport 还没有有效宽度，按窗口宽度估一个初始值。
        QWidget* top = window();
        const int windowWidth = top ? top->width() : width();
        panelW = windowWidth >= 1920 ? 260 : (windowWidth >= 1280 ? 220 : 200);
    }
    updateGridMetrics(panelW);
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
    return QRect(cellRect.left() + m_thumbMarginH, cellRect.top() + m_topMargin, m_thumbSize,
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
    // 拼接模式下新添加的图片默认不参与拼接，由用户手动点击加入。
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

        // 显示/隐藏
        bool allHidden = std::all_of(selected.begin(), selected.end(), [](const QModelIndex& idx) {
            return idx.data(ImageListModel::HiddenRole).toBool();
        });
        menu.addAction(allHidden ? tr("Show") : tr("Hide"), this, [this, selected, allHidden]() {
            m_model->setHiddenForRows(selected, !allHidden);
        });
        menu.addSeparator();

        menu.addAction(tr("Duplicate"), this,
                       [this, selected]() { m_model->duplicateItems(selected); });
        menu.addAction(tr("Copy to clipboard"), this, [selected]() {
            QStringList paths;
            for (const QModelIndex& idx : selected)
                paths.append(idx.data(ImageListModel::FilePathRole).toString());
            QGuiApplication::clipboard()->setText(paths.join(QStringLiteral("\n")));
        });
        menu.addSeparator();

        menu.addAction(tr("Move to front"), this,
                       [this, selected]() { m_model->moveToFront(selected); });
        menu.addAction(tr("Move to back"), this,
                       [this, selected]() { m_model->moveToBack(selected); });
        menu.addSeparator();

        if (single) {
            // 旋转/翻转/上下移只作用于单张图片，统一发信号交给 MainWindow 处理。
            // 此前这些信号在 MainWindow 里已连接，但菜单里没有对应项、也从未 emit，
            // 导致功能完全不可达。
            const int row = selected.first().row();
            menu.addAction(tr("Rotate Left 90°"), this,
                           [this, row]() { emit rotateRequested(row); });
            menu.addAction(tr("Flip Horizontal"), this,
                           [this, row]() { emit flipHorizontalRequested(row); });
            menu.addAction(tr("Flip Vertical"), this,
                           [this, row]() { emit flipVerticalRequested(row); });
            menu.addSeparator();
            menu.addAction(tr("Move Up"), this, [this, row]() { emit moveUpRequested(row); });
            menu.addAction(tr("Move Down"), this, [this, row]() { emit moveDownRequested(row); });
            menu.addSeparator();
        }

        if (single) {
            QString filePath = selected.first().data(ImageListModel::FilePathRole).toString();
            menu.addAction(tr("Show in Explorer"), this, [filePath]() {
                if (!filePath.isEmpty())
                    ExportManager::showInFolder(filePath);
            });
            menu.addAction(tr("Quick Look"), this, [filePath]() {
                if (!filePath.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
            });
            menu.addSeparator();
        } else {
            QString firstPath = selected.first().data(ImageListModel::FilePathRole).toString();
            menu.addAction(tr("Show in Explorer"), this, [firstPath]() {
                if (!firstPath.isEmpty())
                    ExportManager::showInFolder(firstPath);
            });
            menu.addAction(tr("Quick Look"), this, [firstPath]() {
                if (!firstPath.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(firstPath));
            });
            menu.addSeparator();
        }

        menu.addAction(tr("Remove"), this, [this, selected]() { emit removeRequested(selected); });
    } else {
        menu.addAction(tr("Add Images"), this, [this]() { emit addImagesRequested(); });
        menu.addAction(tr("Select All"), this, [this]() { m_view->selectAll(); });
        if (m_model->rowCount() > 0) {
            menu.addSeparator();
            menu.addAction(tr("Clear List"), this, [this]() { emit clearRequested(); });
        }
    }
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
