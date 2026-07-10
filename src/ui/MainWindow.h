#pragma once

#include "ToolBarWidget.h"
#include "core/ImageListModel.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <QProgressDialog>
#include <QSize>
#include <QTimer>
#include <functional>

class QSplitter;
class QLabel;
class QProgressDialog;
class QStackedWidget;

namespace yingtu {

class ImageListWidget;
class PreviewWidget;
class PropertyPanel;
class StatusBarWidget;
class ToolBarWidget;
class ImageEditorWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralWidget();
    void setupStatusBar();
    void setupPreviewToolBar();
    void connectSignals();

    void onAddImages();
    void onRemoveImage();
    void onClearImages();
    void onToolChanged(ToolType tool);
    void onPreviewRequested();
    void onProcessRequested();
    void onBatchProcess();

    // 将引擎处理移到后台线程，避免主线程被批量处理阻塞
    template<typename EngineType, typename SettingsType>
    void runEngineAsync(const SettingsType& settings, const QStringList& paths,
                        const QString& progressTitle,
                        std::function<void(const decltype(std::declval<EngineType>().process(std::declval<QStringList>()))&)> onFinished,
                        bool showProgressDialog = true);

    void onImageDoubleClicked(int row);
    void onImageSelected(int row);
    void updatePreview(bool applyToolEffect = false);
    void updateToolPreview();
    void updateStatusBar();

    void onPreviewZoomIn();
    void onPreviewZoomOut();
    void onPreviewResetZoom();
    void onPreviewFitToWindow();
    void onThumbnailLoadStarted(int total);
    void onThumbnailLoadProgress(int current, int total);
    void onThumbnailLoadFinished();
    void onPreviewFinished();
    void requestDelayedPreview();
    void onPreviewRotateLeft();
    void onPreviewRotateRight();
    void onPreviewFlipHorizontal();
    void onPreviewFlipVertical();
    void onPreviewDelete();

    ImageListModel* m_model = nullptr;
    ImageListWidget* m_listWidget = nullptr;
    PreviewWidget* m_previewWidget = nullptr;
    PropertyPanel* m_propertyPanel = nullptr;
    StatusBarWidget* m_statusBar = nullptr;
    ToolBarWidget* m_toolBar = nullptr;
    QWidget* m_previewToolBar = nullptr;
    QLabel* m_zoomLabel = nullptr;
    ImageEditorWidget* m_editorWidget = nullptr;
    QStackedWidget* m_centerStack = nullptr;

    ToolType m_currentTool = ToolType::Stitch;
    int m_currentImageRow = -1;
    QTimer* m_previewDelayTimer = nullptr;
    QFutureWatcher<struct PreviewTaskResult>* m_previewWatcher = nullptr;
    QFutureWatcher<QSize>* m_stitchSizeWatcher = nullptr;
    QProgressDialog* m_progressDialog = nullptr;
};

} // namespace yingtu
