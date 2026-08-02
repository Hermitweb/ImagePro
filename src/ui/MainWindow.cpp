#include "MainWindow.h"
#include "ImageEditorWidget.h"
#include "ImageListWidget.h"
#include "PreviewWidget.h"
#include "PropertyPanel.h"
#include "StatusBarWidget.h"
#include "ToolBarWidget.h"
#include "dialogs/ResultDialog.h"
#include "app/ImageProApp.h"
#include "app/ThemeManager.h"
#include "app/ToastManager.h"
#include "app/VpkUpdateManager.h"
#include "core/CompressEngine.h"
#include "core/ConvertEngine.h"
#include "core/ExportManager.h"
#include "core/PdfEngine.h"
#include "core/ResizeEngine.h"
#include "core/StitchEngine.h"
#include "core/WatermarkEngine.h"
#include "utils/FileUtils.h"
#include "utils/FlowLayout.h"
#include "utils/ImageLoader.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QSplitter>
#include <QRect>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#ifndef IMAGEPRO_VERSION
#define IMAGEPRO_VERSION "0.0.0-unknown"
#endif

namespace yingtu {

static QString commonParentDirectory(const QStringList& paths)
{
    if (paths.isEmpty())
        return QString();

    QStringList dirs;
    dirs.reserve(paths.size());
    for (const QString& path : paths) {
        QString dir = QFileInfo(path).absolutePath();
        if (!dir.endsWith(QLatin1Char('/')))
            dir += QLatin1Char('/');
        dirs.append(dir);
    }

    QString common = dirs.first();
    for (int i = 1; i < dirs.size(); ++i) {
        const QString& dir = dirs.at(i);
        int len = qMin(common.length(), dir.length());
        int j = 0;
        while (j < len && common.at(j) == dir.at(j))
            ++j;
        common = common.left(j);
    }

    // 截断到最后一个完整目录分隔符
    int lastSep = common.lastIndexOf(QLatin1Char('/'));
    if (lastSep > 0)
        common = common.left(lastSep);
    else if (lastSep == 0 && common.length() > 1)
        common = common.left(1);

    return common;
}

static QString buildImageSaveFilter(const QString& preferredFmt)
{
    QString fmt = preferredFmt.toLower();
    if (fmt == QStringLiteral("jpg"))
        fmt = QStringLiteral("jpeg");

    const char* context = "yingtu::MainWindow";
    struct FilterEntry {
        QString key;
        QString text;
    };
    const QList<FilterEntry> entries = {
        { QStringLiteral("jpeg"), QCoreApplication::translate(context, "JPEG Images (*.jpg *.jpeg)") },
        { QStringLiteral("png"), QCoreApplication::translate(context, "PNG Images (*.png)") },
        { QStringLiteral("bmp"), QCoreApplication::translate(context, "BMP Images (*.bmp)") },
        { QStringLiteral("webp"), QCoreApplication::translate(context, "WebP Images (*.webp)") },
    };

    QStringList filters;
    for (const auto& e : entries) {
        if (e.key == fmt)
            filters.prepend(e.text);
        else
            filters.append(e.text);
    }
    return filters.join(QStringLiteral(";;"));
}

struct PreviewTaskInput {
    ToolType tool;
    QString currentFilePath;
    QStringList allFilePaths;
    QSize previewSize;
    bool applyToolEffect = false;
    StitchSettings stitchSettings;
    CompressSettings compressSettings;
    WatermarkSettings watermarkSettings;
    ResizeSettings resizeSettings;
};

struct PreviewTaskResult {
    QImage image;
    bool comparisonMode = false;
    QImage originalImage;
    QString sourcePath;
    int sourceRotation = 0;
    bool sourceFlippedH = false;
    bool sourceFlippedV = false;
    QVector<QRect> stitchInputRects;
};

static PreviewTaskResult generatePreview(const PreviewTaskInput& input)
{
    PreviewTaskResult result;

    // 左侧切换图片时只加载单张原图，不触发 Stitch/Compress 等全量工具效果
    if (!input.applyToolEffect || input.tool == ToolType::Convert) {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        result.image = item.loadPreviewImage(input.previewSize);
        result.sourcePath = input.currentFilePath;
        result.sourceRotation = item.rotation();
        result.sourceFlippedH = item.flippedHorizontal();
        result.sourceFlippedV = item.flippedVertical();
        return result;
    }

    switch (input.tool) {
    case ToolType::Stitch: {
        const int stitchMaxLongEdge = qMax(input.previewSize.width(), input.previewSize.height());
        result.image = StitchEngine::preview(input.allFilePaths, input.stitchSettings,
                                              stitchMaxLongEdge);
        const int count = input.allFilePaths.size();
        if (count > 0 && !result.image.isNull()) {
            const int w = result.image.width();
            const int h = result.image.height();
            switch (input.stitchSettings.direction) {
            case StitchSettings::Vertical: {
                const int rh = h / count;
                for (int i = 0; i < count; ++i) {
                    const int y = i * rh;
                    const int y2 = (i == count - 1) ? h : (i + 1) * rh;
                    result.stitchInputRects.append(QRect(0, y, w, y2 - y));
                }
                break;
            }
            case StitchSettings::Horizontal: {
                const int rw = w / count;
                for (int i = 0; i < count; ++i) {
                    const int x = i * rw;
                    const int x2 = (i == count - 1) ? w : (i + 1) * rw;
                    result.stitchInputRects.append(QRect(x, 0, x2 - x, h));
                }
                break;
            }
            case StitchSettings::Grid: {
                const int rows = qMax(1, input.stitchSettings.gridRows);
                const int cols = qMax(1, input.stitchSettings.gridColumns);
                const int cw = w / cols;
                const int rh = h / rows;
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        if (result.stitchInputRects.size() >= count)
                            break;
                        const int x = c * cw;
                        const int x2 = (c == cols - 1) ? w : (c + 1) * cw;
                        const int y = r * rh;
                        const int y2 = (r == rows - 1) ? h : (r + 1) * rh;
                        result.stitchInputRects.append(QRect(x, y, x2 - x, y2 - y));
                    }
                }
                break;
            }
            }
        }
        break;
    }
    case ToolType::Compress: {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        QImage original = item.loadPreviewImage(input.previewSize);
        result.originalImage = original;
        result.image = CompressEngine::preview(original, input.compressSettings);
        result.comparisonMode = input.compressSettings.showOriginal;
        break;
    }
    case ToolType::Watermark: {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        QImage img = item.loadPreviewImage(input.previewSize);
        result.image = WatermarkEngine::preview(img, input.watermarkSettings);
        break;
    }
    case ToolType::Resize: {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        QImage img = item.loadPreviewImage(input.previewSize);
        result.image = ResizeEngine::resize(img, input.resizeSettings);
        break;
    }
    case ToolType::Edit: {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        result.image = item.loadImage();
        break;
    }
    default:
        break;
    }

    return result;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("影图 ImagePro"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    resize(1280, 800);
    setMinimumSize(800, 600);

    m_model = new ImageListModel(this);

    setupMenuBar();
    setupToolBar();
    setupPreviewToolBar();
    setupCentralWidget();
    setupStatusBar();

    m_previewDelayTimer = new QTimer(this);
    m_previewDelayTimer->setSingleShot(true);
    m_previewDelayTimer->setInterval(200);

    m_previewWatcher = new QFutureWatcher<PreviewTaskResult>(this);
    connect(m_previewWatcher, &QFutureWatcher<PreviewTaskResult>::finished,
            this, &MainWindow::onPreviewFinished);

    m_stitchSizeWatcher = new QFutureWatcher<QSize>(this);

    connectSignals();

    updateStatusBar();

    // 启动后延迟后台静默检查更新（仅 release 构建启用 Velopack 时）。
    // 用户若先手动触发检查则跳过，避免并发竞态。
    if (VpkUpdateManager::instance().isVelopackEnabled()) {
        QTimer::singleShot(3000, this, [this]() {
            if (m_manualUpdateRequested)
                return;
            m_silentUpdateCheck = true;
            VpkUpdateManager::instance().checkForUpdates();
        });
    }
}

void MainWindow::setupMenuBar()
{
    QMenuBar* bar = menuBar();

    QMenu* fileMenu = bar->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Add Images"), this, &MainWindow::onAddImages, QKeySequence::Open);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);

    QMenu* editMenu = bar->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Undo"), this, [this]() {
        if (m_editorWidget) m_editorWidget->undo();
    }, QKeySequence::Undo);
    editMenu->addAction(tr("&Redo"), this, [this]() {
        if (m_editorWidget) m_editorWidget->redo();
    }, QKeySequence::Redo);

    QMenu* toolMenu = bar->addMenu(tr("&Tools"));
    toolMenu->addAction(tr("&Stitch"), this, [this]() { m_toolBar->setCurrentTool(ToolType::Stitch); }, Qt::Key_F1);
    toolMenu->addAction(tr("&Convert"), this, [this]() { m_toolBar->setCurrentTool(ToolType::Convert); }, Qt::Key_F2);
    toolMenu->addAction(tr("&Compress"), this, [this]() { m_toolBar->setCurrentTool(ToolType::Compress); }, Qt::Key_F3);
    toolMenu->addAction(tr("&Watermark"), this, [this]() { m_toolBar->setCurrentTool(ToolType::Watermark); }, Qt::Key_F4);
    toolMenu->addAction(tr("&Edit"), this, [this]() { m_toolBar->setCurrentTool(ToolType::Edit); }, Qt::Key_F5);
    toolMenu->addAction(tr("&Resize"), this, [this]() { m_toolBar->setCurrentTool(ToolType::Resize); }, Qt::Key_F6);
    toolMenu->addAction(tr("&Batch"), this, [this]() { m_toolBar->setCurrentTool(ToolType::Batch); }, Qt::Key_F7);
    toolMenu->addAction(tr("&PDF"), this, [this]() { m_toolBar->setCurrentTool(ToolType::Pdf); }, Qt::Key_F8);

    QMenu* viewMenu = bar->addMenu(tr("&View"));
    viewMenu->addAction(tr("Toggle &Theme"), this, []() {
        ThemeManager::instance().toggleTheme();
    }, Qt::ALT + Qt::SHIFT + Qt::Key_D);

    QMenu* langMenu = viewMenu->addMenu(tr("&Language"));
    for (const QString& code : ImageProApp::supportedLanguages()) {
        QAction* action = langMenu->addAction(ImageProApp::languageName(code), this, [this, code]() {
            auto* app = qobject_cast<ImageProApp*>(qApp);
            if (app && app->currentLanguage() != code) {
                QSettings settings;
                settings.setValue(QStringLiteral("language"), code);
                QMessageBox::information(this, tr("Language Changed"),
                    tr("The language setting has been changed. Please restart the application to apply."));
            }
        });
        action->setCheckable(true);
        action->setChecked(qobject_cast<ImageProApp*>(qApp)->currentLanguage() == code);
    }

    QMenu* helpMenu = bar->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&Check for Updates"), this, [this]() {
        auto& upd = VpkUpdateManager::instance();
        if (!upd.isVelopackEnabled()) {
            QMessageBox::information(this, tr("Check for Updates"),
                                     tr("Auto-update is not available in this build.<br>"
                                        "Current version: %1").arg(upd.currentVersion()));
            return;
        }
        ToastManager::instance().showInfo(tr("Checking for updates..."), this);
        m_silentUpdateCheck = false;
        m_manualUpdateRequested = true;
        upd.checkForUpdates();
    });
    helpMenu->addAction(tr("&About"), this, [this]() {
        QMessageBox::about(this, tr("About 影图 ImagePro"),
                           tr("<h2>影图 ImagePro</h2>"
                              "<p>Version %1</p>"
                              "<p>A powerful image processing tool.</p>")
                               .arg(QString::fromLatin1(IMAGEPRO_VERSION)));
    });
}

void MainWindow::setupToolBar()
{
    m_toolBar = new ToolBarWidget(this);
}

void MainWindow::setupCentralWidget()
{
    QToolBar* tb = new QToolBar(this);
    tb->setMovable(false);
    tb->addWidget(m_toolBar);
    addToolBar(Qt::TopToolBarArea, tb);

    QWidget* central = new QWidget(this);
    QVBoxLayout* vlayout = new QVBoxLayout(central);
    vlayout->setContentsMargins(0, 0, 0, 0);
    vlayout->setSpacing(0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setHandleWidth(6);

    m_listWidget = new ImageListWidget(m_model, splitter);
    m_listWidget->setMinimumWidth(180);
    m_listWidget->setMaximumWidth(280);

    m_centerStack = new QStackedWidget(splitter);
    m_centerStack->setMinimumSize(400, 300);
    m_previewWidget = new PreviewWidget(m_centerStack);
    m_previewWidget->setStitchImageListModel(m_model);
    m_editorWidget = new ImageEditorWidget(m_centerStack);
    m_centerStack->addWidget(m_previewWidget);
    m_centerStack->addWidget(m_editorWidget);
    m_centerStack->setCurrentWidget(m_previewWidget);

    m_propertyPanel = new PropertyPanel(splitter);
    m_propertyPanel->setImageModel(m_model);

    splitter->addWidget(m_listWidget);
    splitter->addWidget(m_centerStack);
    splitter->addWidget(m_propertyPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes(QList<int>() << 220 << 676 << 240);

    if (m_previewToolBar)
        vlayout->addWidget(m_previewToolBar);
    vlayout->addWidget(splitter, 1);
    setCentralWidget(central);
}

void MainWindow::setupStatusBar()
{
    m_statusBar = new StatusBarWidget(this);
    setStatusBar(m_statusBar);
}

void MainWindow::setupPreviewToolBar()
{
    m_previewToolBar = new QWidget(this);
    FlowLayout* flowLayout = new FlowLayout(m_previewToolBar, 4, 6, 4);
    flowLayout->setContentsMargins(6, 4, 6, 4);

    auto addButton = [this, flowLayout](const QString& text, const QString& tooltip, void (MainWindow::*slot)()) {
        QToolButton* btn = new QToolButton(m_previewToolBar);
        btn->setText(text);
        btn->setToolTip(tooltip);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setAutoRaise(true);
        connect(btn, &QToolButton::clicked, this, slot);
        flowLayout->addWidget(btn);
        return btn;
    };

    addButton(tr("Zoom Out"), tr("Zoom Out"), &MainWindow::onPreviewZoomOut);
    addButton(tr("Original Size"), tr("Original Size"), &MainWindow::onPreviewResetZoom);
    addButton(tr("Zoom In"), tr("Zoom In"), &MainWindow::onPreviewZoomIn);
    addButton(tr("Fit"), tr("Fit to Window"), &MainWindow::onPreviewFitToWindow);

    m_zoomLabel = new QLabel(QStringLiteral("100%"), m_previewToolBar);
    m_zoomLabel->setMinimumWidth(56);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    flowLayout->addWidget(m_zoomLabel);

    QFrame* separator1 = new QFrame(m_previewToolBar);
    separator1->setFrameShape(QFrame::VLine);
    separator1->setFrameShadow(QFrame::Sunken);
    flowLayout->addWidget(separator1);

    addButton(tr("Rotate Left"), tr("Rotate Left"), &MainWindow::onPreviewRotateLeft);
    addButton(tr("Rotate Right"), tr("Rotate Right"), &MainWindow::onPreviewRotateRight);
    addButton(tr("Flip Horizontal"), tr("Flip Horizontal"), &MainWindow::onPreviewFlipHorizontal);
    addButton(tr("Flip Vertical"), tr("Flip Vertical"), &MainWindow::onPreviewFlipVertical);

    QFrame* separator2 = new QFrame(m_previewToolBar);
    separator2->setFrameShape(QFrame::VLine);
    separator2->setFrameShadow(QFrame::Sunken);
    flowLayout->addWidget(separator2);

    addButton(tr("Delete"), tr("Delete Current Image"), &MainWindow::onPreviewDelete);

    m_previewToolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void MainWindow::connectSignals()
{
    connect(m_toolBar, &ToolBarWidget::addImagesClicked, this, &MainWindow::onAddImages);
    connect(m_toolBar, &ToolBarWidget::removeImageClicked, this, &MainWindow::onRemoveImage);
    connect(m_toolBar, &ToolBarWidget::clearImagesClicked, this, &MainWindow::onClearImages);
    connect(m_toolBar, &ToolBarWidget::toolChanged, this, &MainWindow::onToolChanged);

    connect(m_listWidget, &ImageListWidget::imageDoubleClicked, this, &MainWindow::onImageDoubleClicked);
    connect(m_listWidget, &ImageListWidget::imageSelected, this, &MainWindow::onImageSelected);
    connect(m_listWidget, &ImageListWidget::imageSelectionChanged, this, &MainWindow::updateStatusBar);
    connect(m_listWidget, &ImageListWidget::deleteRequested, this, [this](int row) {
        m_model->removeImage(row);
        if (m_currentImageRow >= m_model->rowCount())
            m_currentImageRow = m_model->rowCount() - 1;
        updatePreview();
    });
    connect(m_listWidget, &ImageListWidget::rotateRequested, this, [this](int row) { m_model->rotateItem(row); });
    connect(m_listWidget, &ImageListWidget::flipHorizontalRequested,
            this, [this](int row) { m_model->flipHorizontalItem(row); });
    connect(m_listWidget, &ImageListWidget::flipVerticalRequested,
            this, [this](int row) { m_model->flipVerticalItem(row); });
    connect(m_listWidget, &ImageListWidget::moveUpRequested,
            this, [this](int row) { m_model->moveUp(row); });
    connect(m_listWidget, &ImageListWidget::moveDownRequested,
            this, [this](int row) { m_model->moveDown(row); });
    connect(m_listWidget, &ImageListWidget::addImagesRequested, this, &MainWindow::onAddImages);
    connect(m_listWidget, &ImageListWidget::clearRequested, this, &MainWindow::onClearImages);

    connect(m_model, &ImageListModel::countChanged, this, &MainWindow::updateStatusBar);
    connect(m_model, &ImageListModel::selectionChanged, this, &MainWindow::updateStatusBar);
    connect(m_model, &ImageListModel::thumbnailLoadStarted, this, &MainWindow::onThumbnailLoadStarted);
    connect(m_model, &ImageListModel::thumbnailLoadProgress, this, &MainWindow::onThumbnailLoadProgress);
    connect(m_model, &ImageListModel::thumbnailLoadFinished, this, &MainWindow::onThumbnailLoadFinished);

    connect(m_propertyPanel, &PropertyPanel::previewRequested, this, &MainWindow::onPreviewRequested);
    connect(m_propertyPanel, &PropertyPanel::processRequested, this, &MainWindow::onProcessRequested);
    connect(m_propertyPanel, &PropertyPanel::settingsChanged, this, [this]() {
        if (m_currentTool == ToolType::Edit && m_editorWidget)
            m_editorWidget->setCurrentTool(m_propertyPanel->currentEditAction());
        if (m_previewDelayTimer)
            m_previewDelayTimer->start();
    });
    connect(m_previewDelayTimer, &QTimer::timeout, this, &MainWindow::updateToolPreview);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updatePreview();
    });

    connect(m_previewWidget, &PreviewWidget::zoomChanged, this, [this](double factor) {
        if (m_zoomLabel && m_currentTool != ToolType::Edit)
            m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(factor * 100)));
    });
    connect(m_editorWidget, &ImageEditorWidget::zoomChanged, this, [this](double factor) {
        if (m_zoomLabel && m_currentTool == ToolType::Edit)
            m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(factor * 100)));
    });
    connect(m_previewWidget, &PreviewWidget::deleteCurrentRequested, this, [this]() {
        if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
            m_model->removeImage(m_currentImageRow);
            if (m_currentImageRow >= m_model->rowCount())
                m_currentImageRow = m_model->rowCount() - 1;
            updatePreview();
        }
    });
    connect(m_previewWidget, &PreviewWidget::rotateCurrentRequested, this, [this]() {
        if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount())
            m_model->rotateItem(m_currentImageRow);
    });
    connect(m_previewWidget, &PreviewWidget::rotateCurrentRightRequested, this, [this]() {
        if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
            m_model->rotateItem(m_currentImageRow);
            m_model->rotateItem(m_currentImageRow);
            m_model->rotateItem(m_currentImageRow);
        }
    });

    connect(m_previewWidget, &PreviewWidget::stitchInputImageClicked, this, [this](int index) {
        if (index >= 0 && index < m_model->rowCount())
            onImageSelected(index);
    });
    connect(m_previewWidget, &PreviewWidget::stitchInputImageDoubleClicked, this, [this](int index) {
        if (index >= 0 && index < m_model->rowCount())
            onImageSelected(index);
    });
    connect(m_previewWidget, &PreviewWidget::stitchRotateInputImageRequested,
            this, [this](int index, bool left) {
        if (index < 0 || index >= m_model->rowCount())
            return;
        if (left) {
            m_model->rotateItem(index);
        } else {
            m_model->rotateItem(index);
            m_model->rotateItem(index);
            m_model->rotateItem(index);
        }
        updateToolPreview();
    });
    connect(m_previewWidget, &PreviewWidget::stitchFlipInputImageHorizontalRequested,
            this, [this](int index) {
        if (index >= 0 && index < m_model->rowCount()) {
            m_model->flipHorizontalItem(index);
            updateToolPreview();
        }
    });
    connect(m_previewWidget, &PreviewWidget::stitchFlipInputImageVerticalRequested,
            this, [this](int index) {
        if (index >= 0 && index < m_model->rowCount()) {
            m_model->flipVerticalItem(index);
            updateToolPreview();
        }
    });
    connect(m_previewWidget, &PreviewWidget::stitchRemoveInputImageRequested,
            this, [this](int index) {
        if (index >= 0 && index < m_model->rowCount()) {
            m_model->removeImage(index);
            if (m_currentImageRow >= m_model->rowCount())
                m_currentImageRow = m_model->rowCount() - 1;
            updateToolPreview();
        }
    });
    connect(m_previewWidget, &PreviewWidget::stitchInputImageInfoRequested,
            this, [this](int index) {
        if (index < 0 || index >= m_model->rowCount())
            return;
        const ImageItem* item = m_model->itemAt(index);
        if (!item)
            return;
        QSize size(item->width(), item->height());
        QMessageBox::information(this, tr("Image Info"),
            tr("File: %1\nSize: %2x%3").arg(item->displayName()).arg(size.width()).arg(size.height()));
    });
    connect(m_previewWidget, &PreviewWidget::stitchImageDropped,
            this, [this](const QStringList& paths) {
        if (!paths.isEmpty()) {
            m_model->addImages(paths);
            if (m_currentTool == ToolType::Stitch)
                updateToolPreview();
        }
    });

    connect(m_editorWidget, &ImageEditorWidget::historyChanged,
            m_propertyPanel, &PropertyPanel::refreshEditHistory);
    connect(m_propertyPanel, &PropertyPanel::editUndoRequested, m_editorWidget, &ImageEditorWidget::undo);
    connect(m_propertyPanel, &PropertyPanel::editRedoRequested, m_editorWidget, &ImageEditorWidget::redo);
    connect(m_propertyPanel, &PropertyPanel::editClearRequested, m_editorWidget, &ImageEditorWidget::clearActions);
    connect(m_propertyPanel, &PropertyPanel::editHistoryJumpRequested,
            m_editorWidget, &ImageEditorWidget::jumpToHistoryIndex);

    // ── Velopack 自动更新信号 ─────────────────────────────
    auto& upd = VpkUpdateManager::instance();
    connect(&upd, &VpkUpdateManager::updateAvailable, this, [this](const QString& version) {
        if (m_silentUpdateCheck) {
            // 后台检查：仅 toast 提示，不打断用户。
            ToastManager::instance().showInfo(
                tr("New version %1 available. Use Help → Check for Updates to install.").arg(version), this);
            return;
        }
        const auto ret = QMessageBox::question(
            this, tr("Update Available"),
            tr("A new version %1 is available. Download and apply on exit?").arg(version),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes)
            VpkUpdateManager::instance().downloadAndApplyOnExit();
    });
    connect(&upd, &VpkUpdateManager::upToDate, this, [this]() {
        // 后台检查的“已是最新”不打扰用户。
        if (!m_silentUpdateCheck)
            ToastManager::instance().showInfo(tr("You are running the latest version."), this);
    });
    connect(&upd, &VpkUpdateManager::checkFailed, this, [this](const QString& msg) {
        if (!m_silentUpdateCheck)
            ToastManager::instance().showWarning(msg, this);
    });
    connect(&upd, &VpkUpdateManager::downloadProgress, this, [this](int percent) {
        if (m_statusBar) {
            m_statusBar->setProgressVisible(true);
            m_statusBar->setProgress(percent);
        }
    });
    connect(&upd, &VpkUpdateManager::applyReady, this, [this]() {
        if (m_statusBar)
            m_statusBar->setProgressVisible(false);
        ToastManager::instance().showInfo(
            tr("Update ready. It will be applied when you quit the app."), this);
    });
}

void MainWindow::onAddImages()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Select Images"),
                                                      QDir::homePath(),
                                                      FileUtils::imageFileFilter());
    if (!files.isEmpty()) {
        m_model->addImages(files);
    }
}

void MainWindow::onRemoveImage()
{
    m_model->removeImages(m_listWidget->selectedIndexes());
}

void MainWindow::onClearImages()
{
    m_model->clear();
    m_currentImageRow = -1;
    m_previewWidget->clear();
}

void MainWindow::onToolChanged(ToolType tool)
{
    m_currentTool = tool;
    m_propertyPanel->setToolType(tool);

    m_previewWidget->setStitchMode(tool == ToolType::Stitch);

    if (tool == ToolType::Edit) {
        m_centerStack->setCurrentWidget(m_editorWidget);
        m_editorWidget->setCurrentTool(m_propertyPanel->currentEditAction());
        if (m_zoomLabel)
            m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_editorWidget->zoomFactor() * 100)));
        if (m_currentImageRow >= 0) {
            const ImageItem* item = m_model->itemAt(m_currentImageRow);
            if (item) {
                QString path = item->filePath();
                QApplication::setOverrideCursor(Qt::WaitCursor);
                auto* watcher = new QFutureWatcher<QImage>(this);
                connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher]() {
                    QImage img = watcher->result();
                    if (!img.isNull())
                        m_editorWidget->setBaseImage(img);
                    QApplication::restoreOverrideCursor();
                    watcher->deleteLater();
                });
                watcher->setFuture(QtConcurrent::run([path]() -> QImage {
                    return ImageLoader::loadImage(path);
                }));
            }
        }
    } else if (tool == ToolType::Batch) {
        m_centerStack->setCurrentWidget(m_previewWidget);
        onBatchProcess();
    } else if (tool == ToolType::Pdf) {
        m_centerStack->setCurrentWidget(m_previewWidget);
        updatePreview();
    } else {
        m_centerStack->setCurrentWidget(m_previewWidget);
        updateToolPreview();
    }
}

void MainWindow::onPreviewRequested()
{
    updateToolPreview();
}

template<typename EngineType, typename SettingsType>
void MainWindow::runEngineAsync(const SettingsType& settings, const QStringList& paths,
                                const QString& progressTitle,
                                std::function<void(const decltype(std::declval<EngineType>().process(std::declval<QStringList>()))&)> onFinished,
                                bool showProgressDialog)
{
    using ResultType = decltype(std::declval<EngineType>().process(std::declval<QStringList>()));

    QProgressDialog* currentProgressDlg = nullptr;
    if (showProgressDialog) {
        if (m_progressDialog) {
            m_progressDialog->close();
            m_progressDialog->deleteLater();
        }
        m_progressDialog = new QProgressDialog(progressTitle, tr("Cancel"), 0, 100, this);
        m_progressDialog->setWindowModality(Qt::WindowModal);
        m_progressDialog->setMinimumDuration(0);
        currentProgressDlg = m_progressDialog;
    }

    m_statusBar->setState(StatusBarWidget::State::Processing);

    auto* watcher = new QFutureWatcher<ResultType>(this);
    connect(watcher, &QFutureWatcher<ResultType>::finished, this, [this, watcher, onFinished, currentProgressDlg]() {
        onFinished(watcher->result());
        watcher->deleteLater();
        if (currentProgressDlg) {
            currentProgressDlg->close();
            currentProgressDlg->deleteLater();
            if (m_progressDialog == currentProgressDlg)
                m_progressDialog = nullptr;
        }
        m_statusBar->setState(StatusBarWidget::State::Ready);
    });

    auto future = QtConcurrent::run([settings, paths, currentProgressDlg]() -> ResultType {
        EngineType engine;
        engine.setSettings(settings);
        if (currentProgressDlg)
            QObject::connect(&engine, &EngineType::progress, currentProgressDlg, &QProgressDialog::setValue);
        return engine.process(paths);
    });
    watcher->setFuture(future);
}

void MainWindow::onProcessRequested()
{
    QStringList paths = m_model->filePaths();
    if (paths.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please add images first."));
        return;
    }

    QModelIndexList selectedIndexes = m_listWidget->selectedIndexes();
    QStringList selectedPaths;
    for (const QModelIndex& idx : selectedIndexes)
        selectedPaths.append(idx.data(ImageListModel::FilePathRole).toString());
    if (selectedPaths.isEmpty())
        selectedPaths = paths;

    switch (m_currentTool) {
    case ToolType::Stitch: {
        StitchSettings settings = m_propertyPanel->stitchSettings();

        QString fmt = settings.outputFormat.toLower();

        QString defaultPath;
        if (!settings.outputDir.isEmpty()) {
            defaultPath = settings.outputDir + QStringLiteral("/") + settings.baseName
                          + QStringLiteral(".") + fmt;
        } else {
            QString sourceDir = commonParentDirectory(paths);
            if (sourceDir.isEmpty())
                sourceDir = QFileInfo(paths.first()).absolutePath();
            QFileInfo fi(paths.first());
            defaultPath = sourceDir + QStringLiteral("/") + fi.completeBaseName()
                          + QStringLiteral(".") + fmt;
        }

        QString filter;
        if (fmt == QStringLiteral("jpg") || fmt == QStringLiteral("jpeg"))
            filter = tr("JPEG Images (*.jpg *.jpeg)");
        else if (fmt == QStringLiteral("webp"))
            filter = tr("WebP Images (*.webp)");
        else if (fmt == QStringLiteral("bmp"))
            filter = tr("BMP Images (*.bmp)");
        else
            filter = tr("PNG Images (*.png)");

        QString selectedPath = QFileDialog::getSaveFileName(this, tr("Save Stitched Image"),
                                                            defaultPath, filter);
        if (selectedPath.isEmpty())
            break;

        QString expectedSuffix = QStringLiteral(".") + fmt;
        if (!selectedPath.endsWith(expectedSuffix, Qt::CaseInsensitive)) {
            QFileInfo fi(selectedPath);
            selectedPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() + expectedSuffix;
        }

        settings.explicitOutputPath = selectedPath;
        runEngineAsync<StitchEngine>(settings, paths,
            tr("Stitching..."), [this, paths](const QString& out) {
                if (out.isEmpty())
                    return;
                m_statusBar->setMessage(tr("Saved: %1").arg(out));
                ResultInfo info;
                info.title = tr("Stitch Complete");
                info.message = tr("Successfully stitched %1 images").arg(paths.size());
                info.files << out;
                info.totalFileSize = QFileInfo(out).size();
                ImageInfo ii = ImageLoader::loadInfo(out);
                if (ii.valid)
                    info.resolution = QSize(ii.width, ii.height);
                ResultDialog dialog(info, this);
                dialog.exec();
            });
        break;
    }
    case ToolType::Convert: {
        ConvertSettings convertSettings = m_propertyPanel->convertSettings();

        if (selectedPaths.size() == 1) {
            QFileInfo fi(selectedPaths.first());
            QString fmt = convertSettings.targetFormat.toLower();
            QString defaultPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName()
                                  + QStringLiteral(".") + fmt;
            QString filter = buildImageSaveFilter(fmt);
            QString selectedPath = QFileDialog::getSaveFileName(this, tr("Save Converted Image"),
                                                                defaultPath, filter);
            if (selectedPath.isEmpty())
                break;

            QString expectedSuffix = QStringLiteral(".") + convertSettings.targetFormat.toLower();
            if (!selectedPath.endsWith(expectedSuffix, Qt::CaseInsensitive)) {
                QFileInfo outFi(selectedPath);
                selectedPath = outFi.absolutePath() + QStringLiteral("/") + outFi.completeBaseName() + expectedSuffix;
            }
            convertSettings.explicitOutputPath = selectedPath;
        } else {
            QString defaultDir = convertSettings.outputDir;
            if (defaultDir.isEmpty()) {
                if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
                    defaultDir = QFileInfo(m_model->itemAt(m_currentImageRow)->filePath()).absolutePath();
                } else if (!selectedPaths.isEmpty()) {
                    defaultDir = QFileInfo(selectedPaths.first()).absolutePath();
                }
            }
            QString selectedDir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                                    defaultDir);
            if (selectedDir.isEmpty())
                break;
            convertSettings.explicitOutputDir = selectedDir;
        }

        runEngineAsync<ConvertEngine>(convertSettings, selectedPaths,
            tr("Converting..."), [this, selectedPaths](const QStringList& outs) {
                if (outs.isEmpty())
                    return;
                m_statusBar->setMessage(tr("Converted %1 images").arg(outs.size()));
                ResultInfo info;
                info.title = tr("Convert Complete");
                info.message = tr("Successfully converted %1 / %2 images").arg(outs.size()).arg(selectedPaths.size());
                info.files = outs;
                qint64 totalSize = 0;
                for (const QString& p : outs)
                    totalSize += QFileInfo(p).size();
                info.totalFileSize = totalSize;
                ResultDialog dialog(info, this);
                dialog.exec();
            });
        break;
    }
    case ToolType::Compress: {
        CompressSettings compressSettings = m_propertyPanel->compressSettings();

        if (paths.size() == 1) {
            QFileInfo fi(paths.first());
            QString fmt = compressSettings.outputFormat == QStringLiteral("original")
                              ? fi.suffix() : compressSettings.outputFormat;
            if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
                fmt = QStringLiteral("jpeg");
            QString defaultPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName()
                                  + QStringLiteral("_compressed.") + fmt.toLower();
            QString filter = buildImageSaveFilter(fmt);
            QString selectedPath = QFileDialog::getSaveFileName(this, tr("Save Compressed Image"),
                                                                defaultPath, filter);
            if (selectedPath.isEmpty())
                break;
            compressSettings.explicitOutputPath = selectedPath;
        } else {
            QString defaultDir = compressSettings.outputDir;
            if (defaultDir.isEmpty()) {
                if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
                    defaultDir = QFileInfo(m_model->itemAt(m_currentImageRow)->filePath()).absolutePath();
                } else if (!paths.isEmpty()) {
                    defaultDir = QFileInfo(paths.first()).absolutePath();
                }
            }
            QString selectedDir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                                    defaultDir);
            if (selectedDir.isEmpty())
                break;
            compressSettings.explicitOutputDir = selectedDir;
        }

        runEngineAsync<CompressEngine>(compressSettings, paths,
            tr("Compressing..."), [this](const QList<CompressResult>& results) {
                QStringList outs;
                qint64 totalOriginal = 0;
                qint64 totalCompressed = 0;
                for (const auto& r : results) {
                    if (r.success) {
                        outs.append(r.outputPath);
                        totalOriginal += r.originalSize;
                        totalCompressed += r.compressedSize;
                    }
                }
                m_statusBar->setMessage(tr("Compressed %1 images").arg(outs.size()));
                if (outs.isEmpty())
                    return;
                ResultInfo info;
                info.title = tr("Compress Complete");
                int ratio = totalOriginal > 0 ? qRound((1.0 - totalCompressed / double(totalOriginal)) * 100) : 0;
                info.message = tr("Compressed %1 images, saved %2%").arg(outs.size()).arg(ratio);
                info.files = outs;
                info.totalFileSize = totalCompressed;
                ResultDialog dialog(info, this);
                dialog.exec();
            });
        break;
    }
    case ToolType::Watermark: {
        WatermarkSettings watermarkSettings = m_propertyPanel->watermarkSettings();

        if (paths.size() == 1) {
            QFileInfo fi(paths.first());
            QString fmt = watermarkSettings.outputFormat == QStringLiteral("original")
                              ? fi.suffix() : watermarkSettings.outputFormat;
            if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
                fmt = QStringLiteral("jpeg");
            QString defaultPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName()
                                  + QStringLiteral("_watermarked.") + fmt.toLower();
            QString filter = buildImageSaveFilter(fmt);
            QString selectedPath = QFileDialog::getSaveFileName(this, tr("Save Watermarked Image"),
                                                                defaultPath, filter);
            if (selectedPath.isEmpty())
                break;
            watermarkSettings.explicitOutputPath = selectedPath;
        } else {
            QString defaultDir = watermarkSettings.outputDir;
            if (defaultDir.isEmpty()) {
                if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
                    defaultDir = QFileInfo(m_model->itemAt(m_currentImageRow)->filePath()).absolutePath();
                } else if (!paths.isEmpty()) {
                    defaultDir = QFileInfo(paths.first()).absolutePath();
                }
            }
            QString selectedDir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                                    defaultDir);
            if (selectedDir.isEmpty())
                break;
            watermarkSettings.explicitOutputDir = selectedDir;
        }

        runEngineAsync<WatermarkEngine>(watermarkSettings, paths,
            tr("Watermarking..."), [this](const QStringList& outs) {
                if (outs.isEmpty())
                    return;
                m_statusBar->setMessage(tr("Watermarked %1 images").arg(outs.size()));
                ResultInfo info;
                info.title = tr("Watermark Complete");
                info.message = tr("Successfully watermarked %1 images").arg(outs.size());
                info.files = outs;
                qint64 totalSize = 0;
                for (const QString& p : outs)
                    totalSize += QFileInfo(p).size();
                info.totalFileSize = totalSize;
                ResultDialog dialog(info, this);
                dialog.exec();
            });
        break;
    }
    case ToolType::Resize: {
        ResizeSettings resizeSettings = m_propertyPanel->resizeSettings();

        if (paths.size() == 1) {
            QFileInfo fi(paths.first());
            QString fmt = resizeSettings.outputFormat == QStringLiteral("original")
                              ? fi.suffix() : resizeSettings.outputFormat;
            if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
                fmt = QStringLiteral("jpeg");
            QString defaultPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName()
                                  + QStringLiteral("_resized.") + fmt.toLower();
            QString filter = buildImageSaveFilter(fmt);
            QString selectedPath = QFileDialog::getSaveFileName(this, tr("Save Resized Image"),
                                                                defaultPath, filter);
            if (selectedPath.isEmpty())
                break;
            resizeSettings.explicitOutputPath = selectedPath;
        } else {
            QString defaultDir = resizeSettings.outputDir;
            if (defaultDir.isEmpty()) {
                if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
                    defaultDir = QFileInfo(m_model->itemAt(m_currentImageRow)->filePath()).absolutePath();
                } else if (!paths.isEmpty()) {
                    defaultDir = QFileInfo(paths.first()).absolutePath();
                }
            }
            QString selectedDir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                                    defaultDir);
            if (selectedDir.isEmpty())
                break;
            resizeSettings.explicitOutputDir = selectedDir;
        }

        runEngineAsync<ResizeEngine>(resizeSettings, paths,
            tr("Resizing..."), [this](const QStringList& outs) {
                if (outs.isEmpty())
                    return;
                m_statusBar->setMessage(tr("Resized %1 images").arg(outs.size()));
                ResultInfo info;
                info.title = tr("Resize Complete");
                info.message = tr("Successfully resized %1 images").arg(outs.size());
                info.files = outs;
                qint64 totalSize = 0;
                QSize resolution;
                for (const QString& p : outs) {
                    totalSize += QFileInfo(p).size();
                    ImageInfo ii = ImageLoader::loadInfo(p);
                    if (ii.valid && resolution.isNull())
                        resolution = QSize(ii.width, ii.height);
                }
                info.totalFileSize = totalSize;
                info.resolution = resolution;
                ResultDialog dialog(info, this);
                dialog.exec();
            });
        break;
    }
    case ToolType::Pdf: {
        PdfSettings settings = m_propertyPanel->pdfSettings();
        if (settings.outputPath.isEmpty()) {
            QString defaultPath;
            if (selectedPaths.size() == 1) {
                QFileInfo fi(selectedPaths.first());
                defaultPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() + QStringLiteral(".pdf");
            } else {
                QFileInfo fi(selectedPaths.first());
                defaultPath = fi.absolutePath() + QStringLiteral("/output.pdf");
            }
            settings.outputPath = QFileDialog::getSaveFileName(this, tr("Save PDF"), defaultPath,
                                                               tr("PDF Files (*.pdf)"));
            if (settings.outputPath.isEmpty())
                break;
        }
        runEngineAsync<PdfEngine>(settings, selectedPaths,
            tr("Exporting PDF..."), [this, selectedPaths](const QString& out) {
                if (out.isEmpty()) return;
                m_statusBar->setMessage(tr("Exported PDF: %1").arg(out));
                ResultInfo info;
                info.title = tr("PDF Export Complete");
                info.message = tr("Successfully exported %1 images to PDF").arg(selectedPaths.size());
                info.files << out;
                info.totalFileSize = QFileInfo(out).size();
                ResultDialog dialog(info, this);
                dialog.exec();
            });
        break;
    }
    case ToolType::Edit: {
        QImage img = m_editorWidget->renderedImage();
        if (img.isNull()) break;
        QString path = paths.first();
        QFileInfo fi(path);
        QString fmt = fi.suffix();
        QString defaultPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName()
                              + QStringLiteral("_edited.") + fmt;
        QString filter = buildImageSaveFilter(fmt);
        QString out = QFileDialog::getSaveFileName(this, tr("Save Image"), defaultPath, filter);
        if (out.isEmpty())
            break;
        if (ImageLoader::saveImage(img, out)) {
            m_statusBar->setMessage(tr("Saved edited image: %1").arg(out));
            ResultInfo info;
            info.title = tr("Edit Saved");
            info.message = tr("Edited image saved");
            info.files << out;
            info.totalFileSize = QFileInfo(out).size();
            info.resolution = img.size();
            ResultDialog dialog(info, this);
            dialog.exec();
        }
        break;
    }
    case ToolType::Batch: {
        onBatchProcess();
        break;
    }
    default:
        break;
    }
}

void MainWindow::onBatchProcess()
{
    QStringList paths = m_model->filePaths();
    if (paths.isEmpty()) {
        m_statusBar->setMessage(tr("Batch: please add images first"));
        return;
    }

    auto batch = m_propertyPanel->batchSettings();
    if (batch.outputDir.isEmpty()) {
        m_statusBar->setMessage(tr("Batch: please select an output directory"));
        return;
    }

    QDir outDir(batch.outputDir);
    if (!outDir.exists() && !outDir.mkpath(batch.outputDir)) {
        m_statusBar->setMessage(tr("Batch: failed to create output directory %1").arg(batch.outputDir));
        return;
    }

    auto copyToBatchDir = [this, batch, outDir](const QStringList& outs) {
        QStringList copied;
        for (const QString& path : outs) {
            QFileInfo fi(path);
            QString dest = outDir.absoluteFilePath(fi.fileName());
            if (dest == path)
                continue;
            if (QFile::exists(dest))
                QFile::remove(dest);
            if (QFile::copy(path, dest))
                copied.append(dest);
        }

        m_statusBar->setMessage(tr("Batch saved %1 images to %2").arg(copied.size()).arg(batch.outputDir));
        if (!copied.isEmpty())
            ExportManager::showInFolder(copied.first());
    };

    switch (batch.targetTool) {
    case ToolType::Convert: {
        runEngineAsync<ConvertEngine>(m_propertyPanel->convertSettings(), paths,
            tr("Batch converting..."), [this, copyToBatchDir](const QStringList& outs) {
                copyToBatchDir(outs);
            }, false);
        break;
    }
    case ToolType::Compress: {
        runEngineAsync<CompressEngine>(m_propertyPanel->compressSettings(), paths,
            tr("Batch compressing..."), [this, copyToBatchDir](const QList<CompressResult>& results) {
                QStringList outs;
                for (const auto& r : results) {
                    if (r.success)
                        outs.append(r.outputPath);
                }
                copyToBatchDir(outs);
            }, false);
        break;
    }
    case ToolType::Watermark: {
        runEngineAsync<WatermarkEngine>(m_propertyPanel->watermarkSettings(), paths,
            tr("Batch watermarking..."), [this, copyToBatchDir](const QStringList& outs) {
                copyToBatchDir(outs);
            }, false);
        break;
    }
    case ToolType::Resize: {
        runEngineAsync<ResizeEngine>(m_propertyPanel->resizeSettings(), paths,
            tr("Batch resizing..."), [this, copyToBatchDir](const QStringList& outs) {
                copyToBatchDir(outs);
            }, false);
        break;
    }
    default:
        m_statusBar->setMessage(tr("Batch: unsupported target tool"));
        return;
    }
}

void MainWindow::onImageDoubleClicked(int row)
{
    m_currentImageRow = row;
    updatePreview();
}

void MainWindow::onImageSelected(int row)
{
    if (row == m_currentImageRow)
        return;
    m_currentImageRow = row;
    if (m_currentTool == ToolType::Stitch)
        m_previewWidget->setStitchHighlightedInput(row);
    updatePreview();
}

void MainWindow::updatePreview(bool applyToolEffect)
{
    const bool hasImages = m_model->rowCount() > 0;
    if (!hasImages) {
        m_previewWidget->clear();
        return;
    }

    // 拼接模式下始终显示拼接合成效果，点击图片列表仅用于选择/高亮，不切换单张预览。
    if (m_currentTool == ToolType::Stitch)
        applyToolEffect = true;

    const bool hasCurrentRow = m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount();
    const ImageItem* item = hasCurrentRow ? m_model->itemAt(m_currentImageRow) : nullptr;
    if (m_currentTool != ToolType::Stitch && (!hasCurrentRow || !item || !item->isValid())) {
        m_previewWidget->clear();
        return;
    }

    // 如果前一个异步任务还在运行，尝试取消并不再等待。
    // QtConcurrent::run 不支持真正取消 libvips 操作，等待反而会让 UI 被挂起任务阻塞；
    // 直接启动新任务，旧任务结果会被新 future 覆盖而忽略。
    if (m_previewWatcher->isRunning())
        m_previewWatcher->cancel();

    m_previewWidget->setComparisonMode(false);

    PreviewTaskInput input;
    input.tool = m_currentTool;
    input.currentFilePath = item ? item->filePath() : QString();
    input.allFilePaths = m_model->filePaths();
    input.previewSize = m_previewWidget->viewportSize();
    if (input.previewSize.isEmpty())
        input.previewSize = QSize(1280, 720);
    // 限制预览 oversampling，避免大窗口下生成远超屏幕的巨型缓冲。
    input.previewSize *= 2;
    constexpr int kMaxPreviewLongEdge = 3840;
    if (qMax(input.previewSize.width(), input.previewSize.height()) > kMaxPreviewLongEdge) {
        input.previewSize = input.previewSize.scaled(kMaxPreviewLongEdge, kMaxPreviewLongEdge,
                                                      Qt::KeepAspectRatio);
    }
    input.applyToolEffect = applyToolEffect;
    input.stitchSettings = m_propertyPanel->stitchSettings();
    input.compressSettings = m_propertyPanel->compressSettings();
    input.watermarkSettings = m_propertyPanel->watermarkSettings();
    input.resizeSettings = m_propertyPanel->resizeSettings();

    bool needsProgress = applyToolEffect
                         && (m_currentTool == ToolType::Stitch || m_currentTool == ToolType::Compress
                             || m_currentTool == ToolType::Watermark || m_currentTool == ToolType::Resize);
    if (needsProgress) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_statusBar->setState(StatusBarWidget::State::Processing);
        m_statusBar->setMessage(tr("Updating preview..."));
    }

    QFuture<PreviewTaskResult> future = QtConcurrent::run(generatePreview, input);
    m_previewWatcher->setFuture(future);
}

void MainWindow::updateToolPreview()
{
    updatePreview(true);
}

void MainWindow::updateStatusBar()
{
    m_statusBar->setImageCount(m_model->rowCount(), m_model->selectedCount());

    qint64 totalSize = 0;
    QSize outputSize;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const ImageItem* item = m_model->itemAt(i);
        if (item) {
            totalSize += item->fileSize();
            if (i == 0)
                outputSize = QSize(item->width(), item->height());
        }
    }
    m_statusBar->setTotalFileSize(totalSize);

    if (m_currentTool == ToolType::Stitch && m_model->rowCount() > 0) {
        // 拼接输出尺寸可能涉及加载所有图片，放到后台计算避免阻塞状态栏刷新
        if (m_stitchSizeWatcher->isRunning())
            m_stitchSizeWatcher->cancel();
        disconnect(m_stitchSizeWatcher, nullptr, nullptr, nullptr);
        connect(m_stitchSizeWatcher, &QFutureWatcher<QSize>::finished, this, [this]() {
            if (!m_stitchSizeWatcher->isCanceled())
                m_statusBar->setOutputSize(m_stitchSizeWatcher->result());
        });
        QStringList paths = m_model->filePaths();
        StitchSettings settings = m_propertyPanel->stitchSettings();
        // 状态栏尺寸估算只需要一个受限预览，避免加载完整大图
        const int statusMaxLongEdge = 800;
        m_stitchSizeWatcher->setFuture(QtConcurrent::run([paths, settings, statusMaxLongEdge]() -> QSize {
            QImage preview = StitchEngine::preview(paths, settings, statusMaxLongEdge);
            return preview.isNull() ? QSize() : preview.size();
        }));
    } else if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
        const ImageItem* item = m_model->itemAt(m_currentImageRow);
        if (item)
            outputSize = QSize(item->width(), item->height());
    }
    m_statusBar->setOutputSize(outputSize);
}

void MainWindow::onPreviewZoomIn()
{
    if (m_currentTool == ToolType::Edit && m_editorWidget)
        m_editorWidget->zoomIn();
    else
        m_previewWidget->zoomIn();
}

void MainWindow::onPreviewZoomOut()
{
    if (m_currentTool == ToolType::Edit && m_editorWidget)
        m_editorWidget->zoomOut();
    else
        m_previewWidget->zoomOut();
}

void MainWindow::onPreviewResetZoom()
{
    if (m_currentTool == ToolType::Edit && m_editorWidget)
        m_editorWidget->resetZoom();
    else
        m_previewWidget->resetZoom();
}

void MainWindow::onPreviewFitToWindow()
{
    if (m_currentTool == ToolType::Edit && m_editorWidget)
        m_editorWidget->fitToWindow();
    else
        m_previewWidget->fitToWindow();
}

void MainWindow::onThumbnailLoadStarted(int total)
{
    m_statusBar->setState(StatusBarWidget::State::Processing);
    m_statusBar->setMessage(tr("Loading %1 images...").arg(total));
    m_statusBar->setProgressVisible(true);
    m_statusBar->setProgress(0);
}

void MainWindow::onThumbnailLoadProgress(int current, int total)
{
    if (total > 0) {
        m_statusBar->setProgress(qRound(current * 100.0 / total));
        m_statusBar->setMessage(tr("Loading thumbnails %1/%2").arg(current).arg(total));
    }
}

void MainWindow::onThumbnailLoadFinished()
{
    m_statusBar->setState(StatusBarWidget::State::Ready);
    m_statusBar->setMessage(tr("Ready"));
    m_statusBar->setProgressVisible(false);
}

void MainWindow::onPreviewFinished()
{
    PreviewTaskResult result = m_previewWatcher->result();

    QApplication::restoreOverrideCursor();
    m_statusBar->setState(StatusBarWidget::State::Ready);
    m_statusBar->setMessage(tr("Ready"));

    if (m_currentTool == ToolType::Edit) {
        m_editorWidget->setBaseImage(result.image);
        return;
    }

    if (m_currentTool == ToolType::Stitch) {
        m_previewWidget->setComparisonMode(false);
        m_previewWidget->setStitchSynthesizedImage(result.image);
        m_previewWidget->setStitchInputRects(result.stitchInputRects);
        m_propertyPanel->updatePreview(result.image);
        return;
    }

    if (result.comparisonMode && !result.originalImage.isNull()) {
        m_previewWidget->setOriginalImage(result.originalImage);
        m_previewWidget->setComparisonMode(true);
    } else {
        m_previewWidget->setComparisonMode(false);
    }

    if (!result.sourcePath.isEmpty()) {
        m_previewWidget->setSourcePath(result.sourcePath, result.sourceRotation,
                                       result.sourceFlippedH, result.sourceFlippedV);
    } else {
        m_previewWidget->setImage(result.image);
    }
    m_propertyPanel->updatePreview(result.image);
}

void MainWindow::requestDelayedPreview()
{
    if (m_previewDelayTimer)
        m_previewDelayTimer->start();
}

void MainWindow::onPreviewRotateLeft()
{
    if (m_currentTool == ToolType::Edit && m_editorWidget)
        m_editorWidget->rotateLeft();
    else
        m_previewWidget->rotateLeft();
}

void MainWindow::onPreviewRotateRight()
{
    if (m_currentTool == ToolType::Edit && m_editorWidget)
        m_editorWidget->rotateRight();
    else
        m_previewWidget->rotateRight();
}

void MainWindow::onPreviewFlipHorizontal()
{
    m_previewWidget->flipHorizontal();
}

void MainWindow::onPreviewFlipVertical()
{
    m_previewWidget->flipVertical();
}

void MainWindow::onPreviewDelete()
{
    if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
        m_model->removeImage(m_currentImageRow);
        if (m_currentImageRow >= m_model->rowCount())
            m_currentImageRow = m_model->rowCount() - 1;
        updatePreview();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    event->accept();
}

} // namespace yingtu
