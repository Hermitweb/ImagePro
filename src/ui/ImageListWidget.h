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

    ImageListModel* model() const { return m_model; }
    int loadingAngle() const { return m_loadingAngle; }

signals:
    void imageDoubleClicked(int row);
    void imageSelected(int row);
    void imageSelectionChanged();
    void deleteRequested(int row);
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

private slots:
    void showContextMenu(const QPoint& pos);
    void onLoadingTick();
    void onScrollOrResize();
    void onModelCountChanged(int count);

private:
    void setupView();
    void updateEmptyState();
    void buildMenu(QMenu& menu, const QModelIndex& index);
    void updatePanelWidth();
    void loadVisibleRange();

    ImageListModel* m_model = nullptr;
    QListView* m_view = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QTimer* m_loadingTimer = nullptr;
    int m_loadingAngle = 0;
    bool m_windowFilterInstalled = false;
};

} // namespace yingtu
