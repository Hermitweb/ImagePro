#pragma once

#include "core/ImageListModel.h"
#include <QListView>
#include <QWidget>

class QLabel;
class QTimer;
class QRubberBand;

namespace yingtu {

class ImageListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImageListWidget(ImageListModel* model, QWidget* parent = nullptr);

    void setEmptyHint(const QString& hint);
    QModelIndexList selectedIndexes() const;

    // 拼接模式：单击 toggle 选中（参与拼接），双击单张预览。
    void setStitchSelectionMode(bool enabled);
    void selectAllForStitch();
    void clearStitchSelection();
    void invertStitchSelection();

    ImageListModel* model() const { return m_model; }
    int loadingAngle() const { return m_loadingAngle; }
    QSize cellSize() const { return QSize(m_cellWidth, m_cellHeight); }
    QRect thumbnailRect(const QRect& cellRect) const;

signals:
    void imageDoubleClicked(int row);
    void imageSelected(int row);
    void imageSelectionChanged();
    // 批量移除请求。统一走信号而不是在控件内直接改 model，便于 MainWindow
    // 修正 m_currentImageRow 并刷新预览（原先 Delete 键与右键「移除」直接调用
    // m_model->removeImages，绕过了这些处理）。
    void removeRequested(const QModelIndexList& indexes);
    void rotateRequested(int row);
    void flipHorizontalRequested(int row);
    void flipVerticalRequested(int row);
    void moveUpRequested(int row);
    void moveDownRequested(int row);
    void addImagesRequested();
    void clearRequested();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    // 语言切换：重新套用常驻文本。
    void changeEvent(QEvent* event) override;

private slots:
    void showContextMenu(const QPoint& pos);
    void onLoadingTick();
    void onScrollOrResize();
    void onModelCountChanged(int count);

private:
    void updateEmptyState();
    void retranslateUi();
    void buildMenu(QMenu& menu, const QModelIndex& index);
    void updatePanelWidth();
    void updateGridMetrics(int panelWidth);
    void loadVisibleRange();

    ImageListModel* m_model = nullptr;
    QListView* m_view = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QTimer* m_loadingTimer = nullptr;
    int m_loadingAngle = 0;
    bool m_windowFilterInstalled = false;

    int m_cellWidth = 200;
    int m_cellHeight = 60;
    int m_thumbSize = 192;
    int m_thumbMarginH = 4;
    int m_topMargin = 4;
    bool m_stitchSelectionMode = false;
};

} // namespace yingtu
