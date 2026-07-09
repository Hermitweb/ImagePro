#pragma once

#include "core/ImageListModel.h"
#include <QListView>
#include <QWidget>

class QLabel;

namespace yingtu {

class ImageListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImageListWidget(ImageListModel* model, QWidget* parent = nullptr);

    void setEmptyHint(const QString& hint);
    QModelIndexList selectedIndexes() const;

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

private slots:
    void showContextMenu(const QPoint& pos);

private:
    void setupView();
    void updateEmptyState();
    void buildMenu(QMenu& menu, const QModelIndex& index);

    ImageListModel* m_model = nullptr;
    QListView* m_view = nullptr;
    QLabel* m_emptyLabel = nullptr;
};

} // namespace yingtu
