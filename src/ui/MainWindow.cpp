#include "MainWindow.h"
#include "app/ImageProApp.h"
#include "ImageEditorWidget.h"
#include "ImageListWidget.h"
#include "PreviewWidget.h"
#include "PropertyPanel.h"
#include "StatusBarWidget.h"
#include "ToolBarWidget.h"
#include "dialogs/ResultDialog.h"
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
#include "utils/ErrorHandler.h"
#include "utils/FileUtils.h"
#include "utils/FlowLayout.h"
#include "utils/ImageLoader.h"
#include "utils/AppPaths.h"
#include "utils/ImageTaskPools.h"
#include "utils/ImageProError.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QActionGroup>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
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

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
        {QStringLiteral("jpeg"),
         QCoreApplication::translate(context, "JPEG Images (*.jpg *.jpeg)")},
        {QStringLiteral("png"), QCoreApplication::translate(context, "PNG Images (*.png)")},
        {QStringLiteral("bmp"), QCoreApplication::translate(context, "BMP Images (*.bmp)")},
        {QStringLiteral("webp"), QCoreApplication::translate(context, "WebP Images (*.webp)")},
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
    bool forceOriginal = false; // 工具栏「对比」开关：结果附原图供并排比较
    qint64 generation = 0;
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
    QSize sourceSize;
    int sourceRotation = 0;
    bool sourceFlippedH = false;
    bool sourceFlippedV = false;
    QVector<QRect> stitchInputRects;
    qint64 generation = 0;
};

static PreviewTaskResult generatePreview(const PreviewTaskInput& input)
{
    PreviewTaskResult result;
    result.generation = input.generation;

    // 左侧切换图片时只加载单张原图，不触发 Stitch/Compress 等全量工具效果
    if (!input.applyToolEffect || input.tool == ToolType::Convert) {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        result.image = item.loadPreviewImage(input.previewSize);
        result.sourcePath = input.currentFilePath;
        // 尺寸随结果带给 PreviewWidget：免去它在 GUI 线程再读一次文件头。
        result.sourceSize = QSize(item.width(), item.height());
        result.sourceRotation = item.rotation();
        result.sourceFlippedH = item.flippedHorizontal();
        result.sourceFlippedV = item.flippedVertical();
        return result;
    }

    switch (input.tool) {
    case ToolType::Stitch: {
        const int stitchMaxLongEdge = qMax(input.previewSize.width(), input.previewSize.height());
        // 引擎直接返回每张输入图在拼接图中的真实矩形，无需在 UI 层等分猜测。
        result.image = StitchEngine::preview(input.allFilePaths, input.stitchSettings,
                                             stitchMaxLongEdge, &result.stitchInputRects);
        break;
    }
    case ToolType::Compress: {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        QImage original = item.loadPreviewImage(input.previewSize);
        result.originalImage = original;
        result.image = CompressEngine::preview(original, input.compressSettings);
        result.comparisonMode = input.compressSettings.showOriginal || input.forceOriginal;
        break;
    }
    case ToolType::Watermark: {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        QImage img = item.loadPreviewImage(input.previewSize);
        if (input.forceOriginal) {
            result.originalImage = img;
            result.comparisonMode = true;
        }
        result.image = WatermarkEngine::preview(img, input.watermarkSettings);
        break;
    }
    case ToolType::Resize: {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        QImage img = item.loadPreviewImage(input.previewSize);
        if (input.forceOriginal) {
            result.originalImage = img;
            result.comparisonMode = true;
        }
        result.image = ResizeEngine::resize(img, input.resizeSettings);
        break;
    }
    case ToolType::Edit: {
        ImageItem item(input.currentFilePath, false);
        item.reloadInfo();
        result.image = item.loadImage();
        // 供 onPreviewFinished 判断编辑器当前绑定的文档是否变化，
        // 从而决定是否清空标注与历史（同一文档的预览刷新不能清）。
        result.sourcePath = input.currentFilePath;
        break;
    }
    default:
        break;
    }

    return result;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(tr("ImagePro"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    resize(1280, 800);
    setMinimumSize(800, 600);

    // 恢复上次关闭时的窗口几何。closeEvent 一直在保存 geometry，
    // 但此前全项目没有任何地方读取它，属于无效持久化。
    {
        QSettings settings;
        const QByteArray savedGeometry = settings.value(QStringLiteral("geometry")).toByteArray();
        if (!savedGeometry.isEmpty())
            restoreGeometry(savedGeometry);
    }

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
    connect(m_previewWatcher, &QFutureWatcher<PreviewTaskResult>::finished, this,
            &MainWindow::onPreviewFinished);

    // 状态栏刷新防抖：批量加载 100 张时尺寸逐张到达（见 dataChanged 连接处）。
    m_statusBarRefreshTimer = new QTimer(this);
    m_statusBarRefreshTimer->setSingleShot(true);
    m_statusBarRefreshTimer->setInterval(200);
    connect(m_statusBarRefreshTimer, &QTimer::timeout, this, &MainWindow::updateStatusBar);

    // 错误突发结束后的汇总提示（见 showErrorToastCoalesced）。
    m_errorBurstTimer = new QTimer(this);
    m_errorBurstTimer->setSingleShot(true);
    m_errorBurstTimer->setInterval(1200);
    connect(m_errorBurstTimer, &QTimer::timeout, this, &MainWindow::flushSuppressedErrors);

    // 统一错误上报 → 用户可见提示。
    // 各引擎只负责 ErrorHandler::instance().report(ErrorCode)，如何呈现由 UI 层决定：
    // 这样新增错误码无需改动引擎，且所有失败路径都有同一出口，不再静默失败。
    // 引擎在工作线程上报时，这里通过队列连接回到主线程弹提示。
    connect(&ErrorHandler::instance(), &ErrorHandler::errorOccurred, this,
            [this](const ImageProError& error, const QString& /*detail*/) {
                if (error.level() == ErrorLevel::Info)
                    return; // 信息级只记日志，不打扰用户
                showErrorToastCoalesced(error);
            });

    connectSignals();

    // ToolBarWidget 在构造期间已发出 toolChanged(Stitch)，但此时 connectSignals
    // 尚未执行、信号未连接，导致拼接模式（toggle 选择 / 拼接画布 / 清空选择）在
    // 启动时未初始化。这里显式同步一次默认工具状态，确保拼接模式立即可用。
    onToolChanged(ToolType::Stitch);

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

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && !ImageProApp::isLanguageSwitching())
        retranslateUi();
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("ImagePro"));

    // 菜单栏整体重建：菜单项与动作加起来二十多条，逐条维护「控件 ↔ 源串」的
    // 对应关系容易漏；重建则由 setupMenuBar 里的 tr() 自动产出新语言文本。
    // QMenu::addAction(text, receiver, ...) 创建的动作以**菜单**为父对象，
    // 随菜单销毁而回收，重建不会泄漏。
    rebuildMenuBar();
    rebuildPreviewToolBar();
}

void MainWindow::rebuildMenuBar()
{
    QMenuBar* bar = menuBar();
    setMenuBar(nullptr);
    if (bar) {
        bar->clear();
        bar->deleteLater();
    }
    setupMenuBar();
}

void MainWindow::setupMenuBar()
{
    QMenuBar* bar = menuBar();

    QMenu* fileMenu = bar->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Add Images"), this, &MainWindow::onAddImages, QKeySequence::Open);
    fileMenu->addAction(tr("Add Fol&der..."), this, &MainWindow::onAddFolder,
                        QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    m_recentMenu = fileMenu->addMenu(tr("Recent &Files"));
    rebuildRecentFilesMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);

    QMenu* editMenu = bar->addMenu(tr("&Edit"));
    editMenu->addAction(
        tr("&Undo"), this,
        [this]() {
            if (m_editorWidget)
                m_editorWidget->undo();
        },
        QKeySequence::Undo);
    editMenu->addAction(
        tr("&Redo"), this,
        [this]() {
            if (m_editorWidget)
                m_editorWidget->redo();
        },
        QKeySequence::Redo);

    QMenu* toolMenu = bar->addMenu(tr("&Tools"));
    // 工具切换双快捷键：F1-F8（原有）+ Ctrl+K/E/M/B（设计文档第 6 章承诺）。
    auto addToolAction = [this, toolMenu](const QString& text, ToolType tool, QKeySequence ctrl) {
        QAction* action =
            toolMenu->addAction(text, this, [this, tool]() { m_toolBar->setCurrentTool(tool); });
        action->setShortcuts({QKeySequence(static_cast<int>(Qt::Key_F1) + static_cast<int>(tool) -
                                           static_cast<int>(ToolType::Stitch)),
                              ctrl});
    };
    addToolAction(tr("&Stitch"), ToolType::Stitch, QKeySequence(QStringLiteral("Ctrl+K")));
    addToolAction(tr("&Convert"), ToolType::Convert, QKeySequence(QStringLiteral("Ctrl+E")));
    addToolAction(tr("&Compress"), ToolType::Compress, QKeySequence(QStringLiteral("Ctrl+M")));
    addToolAction(tr("&Watermark"), ToolType::Watermark, QKeySequence(QStringLiteral("Ctrl+B")));
    addToolAction(tr("&Edit"), ToolType::Edit, QKeySequence());
    addToolAction(tr("&Resize"), ToolType::Resize, QKeySequence());
    addToolAction(tr("&Batch"), ToolType::Batch, QKeySequence());
    addToolAction(tr("&PDF"), ToolType::Pdf, QKeySequence());

    QMenu* viewMenu = bar->addMenu(tr("&View"));
    // Ctrl+0 = 实际大小 / Ctrl+1 = 适应窗口（修正预览右键菜单里两者的语义颠倒）。
    viewMenu->addAction(tr("Zoom &In"), this, &MainWindow::onPreviewZoomIn, QKeySequence::ZoomIn);
    viewMenu->addAction(tr("Zoom &Out"), this, &MainWindow::onPreviewZoomOut,
                        QKeySequence::ZoomOut);
    viewMenu->addAction(tr("&Actual Size"), this, &MainWindow::onPreviewResetZoom,
                        QKeySequence(QStringLiteral("Ctrl+0")));
    viewMenu->addAction(tr("&Fit to Window"), this, &MainWindow::onPreviewFitToWindow,
                        QKeySequence(QStringLiteral("Ctrl+1")));
    // 全屏：F11 走菜单 action；Space 在 keyPressEvent 里处理（裸键不进菜单，
    // 避免与面板输入框争抢字符）。
    viewMenu->addAction(tr("&Full Screen"), this, &MainWindow::toggleFullScreen,
                        QKeySequence(QStringLiteral("F11")));
    viewMenu->addSeparator();
    // 裸键（L/R/H/V/←/→）统一在 keyPressEvent 分发，避免文本框聚焦时误触发。
    viewMenu->addAction(tr("Rotate &Left"), this, &MainWindow::onPreviewRotateLeft);
    viewMenu->addAction(tr("Rotate &Right"), this, &MainWindow::onPreviewRotateRight);
    viewMenu->addAction(tr("Flip &Horizontal"), this, &MainWindow::onPreviewFlipHorizontal);
    viewMenu->addAction(tr("Flip &Vertical"), this, &MainWindow::onPreviewFlipVertical);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Previous Image"), this, &MainWindow::onPreviousImage);
    viewMenu->addAction(tr("&Next Image"), this, &MainWindow::onNextImage);
    viewMenu->addAction(
        tr("Toggle &Theme"), this, []() { ThemeManager::instance().toggleTheme(); },
        Qt::ALT + Qt::SHIFT + Qt::Key_D);

    QMenu* langMenu = viewMenu->addMenu(tr("&Language"));
    QActionGroup* langGroup = new QActionGroup(langMenu);
    langGroup->setExclusive(true);
    // 取一次并判空：下面循环里此前直接解引用 qobject_cast 的结果，
    // 一旦 qApp 不是 ImageProApp 就是空指针解引用（同文件另一处已做判空）。
    auto* imageProApp = qobject_cast<ImageProApp*>(qApp);
    const QString currentLanguage = imageProApp ? imageProApp->currentLanguage() : QString();
    for (const QString& code : ImageProApp::supportedLanguages()) {
        QAction* action = langMenu->addAction(ImageProApp::languageName(code), this, [code]() {
            auto* app = qobject_cast<ImageProApp*>(qApp);
            if (app && app->currentLanguage() != code) {
                // 直接切换：setLanguage 会装/卸翻译器，Qt 随即向所有控件
                // 投递 LanguageChange，各控件据此重译，无需重启应用。
                app->setLanguage(code);
            }
        });
        action->setCheckable(true);
        action->setActionGroup(langGroup);
        action->setChecked(currentLanguage == code);
    }

    QMenu* helpMenu = bar->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&Check for Updates"), this, [this]() {
        auto& upd = VpkUpdateManager::instance();
        if (!upd.isVelopackEnabled()) {
            QMessageBox::information(this, tr("Check for Updates"),
                                     tr("Auto-update is not available in this build.<br>"
                                        "Current version: %1")
                                         .arg(upd.currentVersion()));
            return;
        }
        ToastManager::instance().showInfo(tr("Checking for updates..."), this);
        m_silentUpdateCheck = false;
        m_manualUpdateRequested = true;
        upd.checkForUpdates();
    });
    helpMenu->addAction(tr("&About"), this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("About ImagePro"));
        dlg.setFixedSize(360, 280);

        QVBoxLayout* layout = new QVBoxLayout(&dlg);
        layout->setSpacing(12);
        layout->setContentsMargins(28, 24, 28, 20);

        // 应用图标
        QLabel* iconLabel = new QLabel(&dlg);
        QPixmap iconPix(QStringLiteral(":/icons/app.png"));
        if (!iconPix.isNull())
            iconLabel->setPixmap(
                iconPix.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(iconLabel);

        // 标题
        QLabel* title = new QLabel(QStringLiteral("<b style='font-size:16px;'>影图</b>"), &dlg);
        title->setTextFormat(Qt::RichText);
        title->setAlignment(Qt::AlignCenter);
        layout->addWidget(title);

        // 版本
        QLabel* version =
            new QLabel(tr("Version %1").arg(QString::fromLatin1(IMAGEPRO_VERSION)), &dlg);
        version->setAlignment(Qt::AlignCenter);
        version->setStyleSheet(QStringLiteral("color: gray;"));
        layout->addWidget(version);

        // 描述
        QLabel* desc = new QLabel(tr("A powerful image processing tool."), &dlg);
        desc->setAlignment(Qt::AlignCenter);
        layout->addWidget(desc);

        // GitHub 链接
        QLabel* link = new QLabel(
            QStringLiteral("<a href=\"https://github.com/Hermitweb/ImagePro\" "
                           "style='text-decoration:none;'>🔗 GitHub: Hermitweb/ImagePro</a>"),
            &dlg);
        link->setTextFormat(Qt::RichText);
        link->setOpenExternalLinks(true);
        link->setAlignment(Qt::AlignCenter);
        layout->addWidget(link);

        // 分隔线
        QFrame* separator = new QFrame(&dlg);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        layout->addWidget(separator);

        // 关闭按钮
        QPushButton* closeBtn = new QPushButton(tr("OK"), &dlg);
        closeBtn->setMinimumWidth(80);
        connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        layout->addWidget(closeBtn, 0, Qt::AlignCenter);

        dlg.exec();
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
    m_previewToolBarLayout = new FlowLayout(m_previewToolBar, 4, 6, 4);
    m_previewToolBarLayout->setContentsMargins(6, 4, 6, 4);
    populatePreviewToolBar();
    m_previewToolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void MainWindow::populatePreviewToolBar()
{
    FlowLayout* flowLayout = m_previewToolBarLayout;

    auto addButton = [this, flowLayout](const QString& text, const QString& tooltip,
                                        void (MainWindow::*slot)()) {
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

    // 对比原图：处理后效果 vs 原图并排。此前只有压缩工具的 showOriginal
    // 能触发 comparisonMode，其他四个处理工具都没有入口。
    m_compareBtn = new QToolButton(m_previewToolBar);
    m_compareBtn->setText(tr("Compare"));
    m_compareBtn->setToolTip(tr("Show the original image side by side"));
    m_compareBtn->setCheckable(true);
    m_compareBtn->setAutoRaise(true);
    m_compareBtn->setEnabled(false);
    connect(m_compareBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_compareOriginal = checked;
        requestDelayedPreview();
    });
    m_previewToolBarLayout->addWidget(m_compareBtn);

    // 最近一次处理结果的入口：完成时只发轻提示，不打断操作流；
    // 需要回看输出清单时点这里（语言切换重建后按 m_lastResult 恢复可用态）。
    m_lastResultBtn = addButton(tr("Results"), tr("Show the result of the last operation"),
                                &MainWindow::onShowLastResult);
    m_lastResultBtn->setEnabled(!m_lastResult.files.isEmpty());
}

void MainWindow::rebuildPreviewToolBar()
{
    if (!m_previewToolBarLayout)
        return;

    // 清空旧按钮后重建：按钮文本与提示都由 tr() 产出，
    // 重建即可整体换成新语言，不必逐个维护对应关系。
    while (QLayoutItem* item = m_previewToolBarLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    populatePreviewToolBar();
}

void MainWindow::connectSignals()
{
    connect(m_toolBar, &ToolBarWidget::addImagesClicked, this, &MainWindow::onAddImages);
    connect(m_toolBar, &ToolBarWidget::removeImageClicked, this, &MainWindow::onRemoveImage);
    connect(m_toolBar, &ToolBarWidget::clearImagesClicked, this, &MainWindow::onClearImages);
    connect(m_toolBar, &ToolBarWidget::toolChanged, this, &MainWindow::onToolChanged);

    connect(m_listWidget, &ImageListWidget::imageDoubleClicked, this,
            &MainWindow::onImageDoubleClicked);
    connect(m_listWidget, &ImageListWidget::imageSelected, this, &MainWindow::onImageSelected);
    connect(m_listWidget, &ImageListWidget::imageSelectionChanged, this, [this]() {
        updateStatusBar();
        // 拼接模式下选择变更（toggle 参与/取消）需要刷新预览。
        if (m_currentTool == ToolType::Stitch)
            m_previewDelayTimer->start();
    });
    connect(m_listWidget, &ImageListWidget::removeRequested, this, &MainWindow::removeImages);
    connect(m_listWidget, &ImageListWidget::pasteRequested, this, &MainWindow::onPasteImage);
    connect(m_listWidget, &ImageListWidget::rotateRequested, this,
            [this](int row) { m_model->rotateItem(row); });
    connect(m_listWidget, &ImageListWidget::flipHorizontalRequested, this,
            [this](int row) { m_model->flipHorizontalItem(row); });
    connect(m_listWidget, &ImageListWidget::flipVerticalRequested, this,
            [this](int row) { m_model->flipVerticalItem(row); });
    connect(m_listWidget, &ImageListWidget::moveUpRequested, this,
            [this](int row) { m_model->moveUp(row); });
    connect(m_listWidget, &ImageListWidget::moveDownRequested, this,
            [this](int row) { m_model->moveDown(row); });
    connect(m_listWidget, &ImageListWidget::addImagesRequested, this, &MainWindow::onAddImages);
    connect(m_listWidget, &ImageListWidget::clearRequested, this, &MainWindow::onClearImages);

    connect(m_model, &ImageListModel::countChanged, this, &MainWindow::updateStatusBar);
    connect(m_model, &ImageListModel::selectionChanged, this, &MainWindow::updateStatusBar);
    // 图片尺寸是**异步**加载出来的：addImages 触发 countChanged 时还不知道尺寸，
    // 状态栏的输出尺寸会一直是空的，直到用户碰巧做了别的操作才被刷新。
    // 这里按尺寸相关角色过滤后刷新。批量加载 100 张时会连发 100 次 ——
    // 用 200ms 单发定时器合并，updateStatusBar 本身是 O(n)，不合并就是 O(n²)。
    connect(m_model, &ImageListModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QVector<int>& roles) {
                if (roles.contains(ImageListModel::WidthRole) ||
                    roles.contains(ImageListModel::HeightRole))
                    m_statusBarRefreshTimer->start();
            });
    connect(m_model, &ImageListModel::thumbnailLoadStarted, this,
            &MainWindow::onThumbnailLoadStarted);
    connect(m_model, &ImageListModel::thumbnailLoadProgress, this,
            &MainWindow::onThumbnailLoadProgress);
    connect(m_model, &ImageListModel::thumbnailLoadFinished, this,
            &MainWindow::onThumbnailLoadFinished);

    connect(m_propertyPanel, &PropertyPanel::previewRequested, this,
            &MainWindow::onPreviewRequested);
    connect(m_propertyPanel, &PropertyPanel::processRequested, this,
            &MainWindow::onProcessRequested);
    connect(m_propertyPanel, &PropertyPanel::stitchSelectAllRequested, this, [this]() {
        m_listWidget->selectAllForStitch();
        if (m_currentTool == ToolType::Stitch)
            updateToolPreview();
    });
    connect(m_propertyPanel, &PropertyPanel::stitchClearSelectionRequested, this, [this]() {
        m_listWidget->clearStitchSelection();
        if (m_currentTool == ToolType::Stitch)
            updateToolPreview();
    });
    connect(m_propertyPanel, &PropertyPanel::stitchInvertSelectionRequested, this, [this]() {
        m_listWidget->invertStitchSelection();
        if (m_currentTool == ToolType::Stitch)
            updateToolPreview();
    });
    connect(m_propertyPanel, &PropertyPanel::settingsChanged, this, [this]() {
        if (m_currentTool == ToolType::Edit && m_editorWidget)
            m_editorWidget->setCurrentTool(m_propertyPanel->currentEditAction());
        if (m_previewDelayTimer)
            m_previewDelayTimer->start();
    });
    connect(m_previewDelayTimer, &QTimer::timeout, this, &MainWindow::updateToolPreview);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this]() { updatePreview(); });

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
    connect(m_previewWidget, &PreviewWidget::stitchInputImageDoubleClicked, this,
            [this](int index) {
                if (index >= 0 && index < m_model->rowCount())
                    onImageSelected(index);
            });
    connect(m_previewWidget, &PreviewWidget::stitchRotateInputImageRequested, this,
            [this](int index, bool left) {
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
    connect(m_previewWidget, &PreviewWidget::stitchFlipInputImageHorizontalRequested, this,
            [this](int index) {
                if (index >= 0 && index < m_model->rowCount()) {
                    m_model->flipHorizontalItem(index);
                    updateToolPreview();
                }
            });
    connect(m_previewWidget, &PreviewWidget::stitchFlipInputImageVerticalRequested, this,
            [this](int index) {
                if (index >= 0 && index < m_model->rowCount()) {
                    m_model->flipVerticalItem(index);
                    updateToolPreview();
                }
            });
    connect(m_previewWidget, &PreviewWidget::stitchRemoveInputImageRequested, this,
            [this](int index) {
                if (index >= 0 && index < m_model->rowCount()) {
                    m_model->removeImage(index);
                    if (m_currentImageRow >= m_model->rowCount())
                        m_currentImageRow = m_model->rowCount() - 1;
                    updateToolPreview();
                }
            });
    connect(m_previewWidget, &PreviewWidget::stitchInputImageInfoRequested, this,
            [this](int index) {
                if (index < 0 || index >= m_model->rowCount())
                    return;
                const ImageItem* item = m_model->itemAt(index);
                if (!item)
                    return;
                QSize size(item->width(), item->height());
                QMessageBox::information(this, tr("Image Info"),
                                         tr("File: %1\nSize: %2x%3")
                                             .arg(item->displayName())
                                             .arg(size.width())
                                             .arg(size.height()));
            });
    connect(m_previewWidget, &PreviewWidget::stitchImageDropped, this,
            [this](const QStringList& paths) {
                if (!paths.isEmpty()) {
                    m_model->addImages(paths);
                    if (m_currentTool == ToolType::Stitch)
                        updateToolPreview();
                }
            });

    connect(m_editorWidget, &ImageEditorWidget::historyChanged, m_propertyPanel,
            &PropertyPanel::refreshEditHistory);
    // 裁剪比例锁：面板复选框 → 编辑器状态。
    connect(m_propertyPanel, &PropertyPanel::cropAspectLockChanged, m_editorWidget,
            &ImageEditorWidget::setCropAspectLocked);
    connect(m_propertyPanel, &PropertyPanel::editUndoRequested, m_editorWidget,
            &ImageEditorWidget::undo);
    connect(m_propertyPanel, &PropertyPanel::editRedoRequested, m_editorWidget,
            &ImageEditorWidget::redo);
    connect(m_propertyPanel, &PropertyPanel::editClearRequested, m_editorWidget,
            &ImageEditorWidget::clearActions);
    connect(m_propertyPanel, &PropertyPanel::editHistoryJumpRequested, m_editorWidget,
            &ImageEditorWidget::jumpToHistoryIndex);

    // ── Velopack 自动更新信号 ─────────────────────────────
    auto& upd = VpkUpdateManager::instance();
    connect(&upd, &VpkUpdateManager::updateAvailable, this, [this](const QString& version) {
        if (m_silentUpdateCheck) {
            // 后台检查：仅 toast 提示，不打断用户。
            ToastManager::instance().showInfo(
                tr("New version %1 available. Use Help → Check for Updates to install.")
                    .arg(version),
                this);
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
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Images"), lastBrowseDirectory(), FileUtils::imageFileFilter());
    if (files.isEmpty())
        return;
    rememberRecentFiles(files);
    m_model->addImages(files);
}

void MainWindow::onAddFolder()
{
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Add Folder"), lastBrowseDirectory());
    if (dir.isEmpty())
        return;
    const QStringList files = FileUtils::expandToImageFiles(QStringList{dir});
    if (files.isEmpty()) {
        ToastManager::instance().showWarning(tr("No supported images found in the folder"), this);
        return;
    }
    rememberRecentFiles(files);
    m_model->addImages(files);
}

void MainWindow::onPasteImage()
{
    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull())
        return;
    // 粘贴截图落盘到应用配置目录：列表以文件为中心，需要持久的实体文件。
    QDir().mkpath(appConfigDir() + QStringLiteral("/clipboard"));
    const QString path = appConfigDir() + QStringLiteral("/clipboard/paste_") +
                         QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")) +
                         QStringLiteral(".png");
    if (!ImageLoader::saveImage(img, path))
        return;
    m_model->addImages(QStringList{path});
}

QString MainWindow::lastBrowseDirectory() const
{
    // 优先用跨工具共享的输出目录历史（ui-state 持久化），比每次都落到
    // 系统默认目录顺手得多。
    const QString recent = m_propertyPanel->recentOutputDirectory();
    return recent.isEmpty() ? QString() : recent;
}

void MainWindow::rememberRecentFiles(const QStringList& files)
{
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& f : files) {
        recent.removeAll(f);
        recent.prepend(f);
    }
    while (recent.size() > 10)
        recent.removeLast();
    settings.setValue(QStringLiteral("recentFiles"), recent);
}

void MainWindow::rebuildRecentFilesMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    QSettings settings;
    const QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    if (recent.isEmpty()) {
        m_recentMenu->addAction(tr("(empty)"))->setEnabled(false);
        return;
    }
    for (const QString& f : recent) {
        m_recentMenu->addAction(f, this, [this, f]() {
            if (QFileInfo::exists(f))
                m_model->addImages(QStringList{f});
        });
    }
}

void MainWindow::onRemoveImage()
{
    removeImages(m_listWidget->selectedIndexes());
}

void MainWindow::removeImages(const QModelIndexList& indexes)
{
    if (indexes.isEmpty())
        return;

    m_model->removeImages(indexes);

    // 删除后必须修正当前行：m_currentImageRow 只在点击/双击时被赋值，删除图片
    // 不会触发它，否则预览区与状态栏可能显示的不是用户最后选中的那张图。
    const int rowCount = m_model->rowCount();
    if (rowCount <= 0) {
        m_currentImageRow = -1;
        m_previewWidget->clear();
        return;
    }
    m_currentImageRow = qBound(0, m_currentImageRow, rowCount - 1);

    if (m_currentTool == ToolType::Stitch) {
        m_previewWidget->setStitchHighlightedInput(m_currentImageRow);
        return;
    }
    updatePreview(false);
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

    // 拼接模式：启用 toggle 选择，默认不选中任何图片（用户手动点击加入拼接）。
    if (tool == ToolType::Stitch)
        m_listWidget->clearStitchSelection();
    m_listWidget->setStitchSelectionMode(tool == ToolType::Stitch);

    if (tool == ToolType::Edit) {
        m_centerStack->setCurrentWidget(m_editorWidget);
        m_editorWidget->setCurrentTool(m_propertyPanel->currentEditAction());
        if (m_zoomLabel)
            m_zoomLabel->setText(
                QStringLiteral("%1%").arg(qRound(m_editorWidget->zoomFactor() * 100)));
        if (m_currentImageRow >= 0) {
            const ImageItem* item = m_model->itemAt(m_currentImageRow);
            if (item) {
                QString path = item->filePath();
                QApplication::setOverrideCursor(Qt::WaitCursor);
                auto* watcher = new QFutureWatcher<QImage>(this);
                connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, path]() {
                    QImage img = watcher->result();
                    if (!img.isNull())
                        m_editorWidget->setDocumentImage(img, path);
                    QApplication::restoreOverrideCursor();
                    watcher->deleteLater();
                });
                watcher->setFuture(
                    QtConcurrent::run(ImageTaskPools::previewPool(),
                                      [path]() -> QImage { return ImageLoader::loadImage(path); }));
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

    // 对比原图仅对单图处理工具有意义（拼接是多图合成、编辑有自己的画布）。
    if (m_compareBtn) {
        const bool comparable = tool == ToolType::Convert || tool == ToolType::Compress ||
                                tool == ToolType::Watermark || tool == ToolType::Resize;
        m_compareBtn->setEnabled(comparable);
        if (!comparable && m_compareOriginal) {
            m_compareOriginal = false;
            m_compareBtn->setChecked(false);
        }
    }
}

void MainWindow::onPreviewRequested()
{
    updateToolPreview();
}

template <typename EngineType, typename SettingsType>
void MainWindow::runEngineAsync(
    const SettingsType& settings, const QStringList& paths, const QString& progressTitle,
    const std::function<void(const EngineResultOf<EngineType>&)>& onFinished,
    bool showProgressDialog)
{
    using ResultType = decltype(std::declval<EngineType>().process(std::declval<QStringList>()));

    // 用 QPointer 而非裸指针：下一次 runEngineAsync 会对上一个对话框执行
    // close()+deleteLater()，裸指针会在完成回调里变成释放后使用。
    QPointer<QProgressDialog> currentProgressDlg;
    // 取消标志：对话框取消按钮置位，引擎在每个文件开工前检查。
    // 引擎是工作线程里的栈对象，用 shared_ptr 保证生命周期覆盖整个执行过程。
    auto cancelFlag = QSharedPointer<std::atomic<bool>>::create(false);
    if (showProgressDialog) {
        if (m_progressDialog) {
            m_progressDialog->close();
            m_progressDialog->deleteLater();
        }
        // 引擎支持协作式取消（每个文件之间检查标志）：恢复取消按钮。
        // 已完成的文件保留，剩余文件跳过 —— 进度照常走到 100% 后正常收尾。
        m_progressDialog = new QProgressDialog(progressTitle, tr("Cancel"), 0, 100, this);
        m_progressDialog->setWindowModality(Qt::WindowModal);
        m_progressDialog->setMinimumDuration(0);
        connect(m_progressDialog, &QProgressDialog::canceled, this,
                [cancelFlag]() { cancelFlag->store(true); });
        currentProgressDlg = m_progressDialog;
    }

    m_statusBar->setState(StatusBarWidget::State::Processing);

    auto* watcher = new QFutureWatcher<ResultType>(this);
    connect(watcher, &QFutureWatcher<ResultType>::finished, this,
            [this, watcher, onFinished, currentProgressDlg]() {
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

    auto future = QtConcurrent::run(
        ImageTaskPools::enginePool(),
        [settings, paths, currentProgressDlg, cancelFlag]() -> ResultType {
            EngineType engine;
            engine.setSettings(settings);
            engine.setCancelFlag(cancelFlag.data());
            // 对话框若已销毁，QPointer 为空，connect 会安全返回 false 而不是崩溃。
            if (currentProgressDlg)
                QObject::connect(&engine, &EngineType::progress, currentProgressDlg.data(),
                                 &QProgressDialog::setValue);
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
        // 拼接仅使用选中的图片，无选中时回退到全部可见图片。
        QStringList stitchPaths = m_model->selectedFilePaths();
        if (stitchPaths.isEmpty())
            stitchPaths = paths;

        StitchSettings settings = m_propertyPanel->stitchSettings();

        QString fmt = settings.outputFormat.toLower();

        QString defaultPath;
        // 文件名模板此前只是装饰：面板里写的 {name}_stitched_{time} 从不生效。
        // 现在用它展开出默认输出名（另存对话框可改），与预览所见一致。
        const QString templatedName = FileUtils::applyFileNameTemplate(
            m_propertyPanel->outputFileNameTemplate(), QFileInfo(stitchPaths.first()).fileName(), 1,
            QSize(), QStringLiteral("stitched"));
        if (!settings.outputDir.isEmpty()) {
            defaultPath = settings.outputDir + QStringLiteral("/") + templatedName +
                          QStringLiteral(".") + fmt;
        } else {
            QString sourceDir = commonParentDirectory(stitchPaths);
            if (sourceDir.isEmpty())
                sourceDir = QFileInfo(stitchPaths.first()).absolutePath();
            defaultPath =
                sourceDir + QStringLiteral("/") + templatedName + QStringLiteral(".") + fmt;
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

        QString selectedPath =
            QFileDialog::getSaveFileName(this, tr("Save Stitched Image"), defaultPath, filter);
        if (selectedPath.isEmpty())
            break;

        QString expectedSuffix = QStringLiteral(".") + fmt;
        if (!selectedPath.endsWith(expectedSuffix, Qt::CaseInsensitive)) {
            QFileInfo fi(selectedPath);
            selectedPath =
                fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() + expectedSuffix;
        }

        settings.explicitOutputPath = selectedPath;
        runEngineAsync<StitchEngine>(
            settings, stitchPaths, tr("Stitching..."), [this, stitchPaths](const QString& out) {
                if (out.isEmpty())
                    return;
                m_statusBar->setMessage(tr("Saved: %1").arg(out));
                ResultInfo info;
                info.title = tr("Stitch Complete");
                info.message = tr("Successfully stitched %1 images").arg(stitchPaths.size());
                info.files << out;
                info.totalFileSize = QFileInfo(out).size();
                ImageInfo ii = ImageLoader::loadInfo(out);
                if (ii.valid)
                    info.resolution = QSize(ii.width, ii.height);
                showProcessResult(info);
            });
        break;
    }
    case ToolType::Convert: {
        ConvertSettings convertSettings = m_propertyPanel->convertSettings();

        if (selectedPaths.size() == 1) {
            QFileInfo fi(selectedPaths.first());
            QString fmt = convertSettings.targetFormat.toLower();
            QString defaultPath =
                (lastBrowseDirectory().isEmpty() ? fi.absolutePath() : lastBrowseDirectory()) +
                QStringLiteral("/") + fi.completeBaseName() + QStringLiteral(".") + fmt;
            QString filter = buildImageSaveFilter(fmt);
            QString selectedPath =
                QFileDialog::getSaveFileName(this, tr("Save Converted Image"), defaultPath, filter);
            if (selectedPath.isEmpty())
                break;

            QString expectedSuffix = QStringLiteral(".") + convertSettings.targetFormat.toLower();
            if (!selectedPath.endsWith(expectedSuffix, Qt::CaseInsensitive)) {
                QFileInfo outFi(selectedPath);
                selectedPath = outFi.absolutePath() + QStringLiteral("/") +
                               outFi.completeBaseName() + expectedSuffix;
            }
            convertSettings.explicitOutputPath = selectedPath;
        } else {
            QString defaultDir = convertSettings.outputDir;
            if (defaultDir.isEmpty()) {
                if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
                    defaultDir =
                        QFileInfo(m_model->itemAt(m_currentImageRow)->filePath()).absolutePath();
                } else if (!selectedPaths.isEmpty()) {
                    defaultDir = QFileInfo(selectedPaths.first()).absolutePath();
                }
            }
            QString selectedDir =
                QFileDialog::getExistingDirectory(this, tr("Select Output Directory"), defaultDir);
            if (selectedDir.isEmpty())
                break;
            convertSettings.explicitOutputDir = selectedDir;
        }

        runEngineAsync<ConvertEngine>(convertSettings, selectedPaths, tr("Converting..."),
                                      [this, selectedPaths](const QStringList& outs) {
                                          if (outs.isEmpty())
                                              return;
                                          m_statusBar->setMessage(
                                              tr("Converted %1 images").arg(outs.size()));
                                          ResultInfo info;
                                          info.title = tr("Convert Complete");
                                          info.message = tr("Successfully converted %1 / %2 images")
                                                             .arg(outs.size())
                                                             .arg(selectedPaths.size());
                                          info.files = outs;
                                          qint64 totalSize = 0;
                                          for (const QString& p : outs)
                                              totalSize += QFileInfo(p).size();
                                          info.totalFileSize = totalSize;
                                          showProcessResult(info);
                                      });
        break;
    }
    case ToolType::Compress: {
        CompressSettings compressSettings = m_propertyPanel->compressSettings();

        if (paths.size() == 1) {
            QFileInfo fi(paths.first());
            QString fmt = compressSettings.outputFormat == QStringLiteral("original")
                              ? fi.suffix()
                              : compressSettings.outputFormat;
            if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
                fmt = QStringLiteral("jpeg");
            QString defaultPath =
                (lastBrowseDirectory().isEmpty() ? fi.absolutePath() : lastBrowseDirectory()) +
                QStringLiteral("/") + fi.completeBaseName() + QStringLiteral("_compressed.") +
                fmt.toLower();
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
                    defaultDir =
                        QFileInfo(m_model->itemAt(m_currentImageRow)->filePath()).absolutePath();
                } else if (!paths.isEmpty()) {
                    defaultDir = QFileInfo(paths.first()).absolutePath();
                }
            }
            QString selectedDir =
                QFileDialog::getExistingDirectory(this, tr("Select Output Directory"), defaultDir);
            if (selectedDir.isEmpty())
                break;
            compressSettings.explicitOutputDir = selectedDir;
        }

        runEngineAsync<CompressEngine>(
            compressSettings, paths, tr("Compressing..."),
            [this](const QList<CompressResult>& results) {
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
                int ratio = totalOriginal > 0
                                ? qRound((1.0 - totalCompressed / double(totalOriginal)) * 100)
                                : 0;
                info.message = tr("Compressed %1 images, saved %2%").arg(outs.size()).arg(ratio);
                info.files = outs;
                info.totalFileSize = totalCompressed;
                showProcessResult(info);
            });
        break;
    }
    case ToolType::Watermark: {
        WatermarkSettings watermarkSettings = m_propertyPanel->watermarkSettings();

        if (paths.size() == 1) {
            QFileInfo fi(paths.first());
            QString fmt = watermarkSettings.outputFormat == QStringLiteral("original")
                              ? fi.suffix()
                              : watermarkSettings.outputFormat;
            if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
                fmt = QStringLiteral("jpeg");
            QString defaultPath =
                (lastBrowseDirectory().isEmpty() ? fi.absolutePath() : lastBrowseDirectory()) +
                QStringLiteral("/") + fi.completeBaseName() + QStringLiteral("_watermarked.") +
                fmt.toLower();
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
                    defaultDir =
                        QFileInfo(m_model->itemAt(m_currentImageRow)->filePath()).absolutePath();
                } else if (!paths.isEmpty()) {
                    defaultDir = QFileInfo(paths.first()).absolutePath();
                }
            }
            QString selectedDir =
                QFileDialog::getExistingDirectory(this, tr("Select Output Directory"), defaultDir);
            if (selectedDir.isEmpty())
                break;
            watermarkSettings.explicitOutputDir = selectedDir;
        }

        runEngineAsync<WatermarkEngine>(
            watermarkSettings, paths, tr("Watermarking..."), [this](const QStringList& outs) {
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
                showProcessResult(info);
            });
        break;
    }
    case ToolType::Resize: {
        ResizeSettings resizeSettings = m_propertyPanel->resizeSettings();

        if (paths.size() == 1) {
            QFileInfo fi(paths.first());
            QString fmt = resizeSettings.outputFormat == QStringLiteral("original")
                              ? fi.suffix()
                              : resizeSettings.outputFormat;
            if (fmt.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0)
                fmt = QStringLiteral("jpeg");
            QString defaultPath =
                (lastBrowseDirectory().isEmpty() ? fi.absolutePath() : lastBrowseDirectory()) +
                QStringLiteral("/") + fi.completeBaseName() + QStringLiteral("_resized.") +
                fmt.toLower();
            QString filter = buildImageSaveFilter(fmt);
            QString selectedPath =
                QFileDialog::getSaveFileName(this, tr("Save Resized Image"), defaultPath, filter);
            if (selectedPath.isEmpty())
                break;
            resizeSettings.explicitOutputPath = selectedPath;
        } else {
            QString defaultDir = resizeSettings.outputDir;
            if (defaultDir.isEmpty()) {
                if (m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount()) {
                    defaultDir =
                        QFileInfo(m_model->itemAt(m_currentImageRow)->filePath()).absolutePath();
                } else if (!paths.isEmpty()) {
                    defaultDir = QFileInfo(paths.first()).absolutePath();
                }
            }
            QString selectedDir =
                QFileDialog::getExistingDirectory(this, tr("Select Output Directory"), defaultDir);
            if (selectedDir.isEmpty())
                break;
            resizeSettings.explicitOutputDir = selectedDir;
        }

        runEngineAsync<ResizeEngine>(
            resizeSettings, paths, tr("Resizing..."), [this](const QStringList& outs) {
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
                showProcessResult(info);
            });
        break;
    }
    case ToolType::Pdf: {
        PdfSettings settings = m_propertyPanel->pdfSettings();
        if (settings.outputPath.isEmpty()) {
            QString defaultPath;
            if (selectedPaths.size() == 1) {
                QFileInfo fi(selectedPaths.first());
                defaultPath = fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() +
                              QStringLiteral(".pdf");
            } else {
                QFileInfo fi(selectedPaths.first());
                defaultPath = fi.absolutePath() + QStringLiteral("/output.pdf");
            }
            settings.outputPath = QFileDialog::getSaveFileName(this, tr("Save PDF"), defaultPath,
                                                               tr("PDF Files (*.pdf)"));
            if (settings.outputPath.isEmpty())
                break;
        }
        runEngineAsync<PdfEngine>(
            settings, selectedPaths, tr("Exporting PDF..."),
            [this, selectedPaths](const QString& out) {
                if (out.isEmpty())
                    return;
                m_statusBar->setMessage(tr("Exported PDF: %1").arg(out));
                ResultInfo info;
                info.title = tr("PDF Export Complete");
                info.message =
                    tr("Successfully exported %1 images to PDF").arg(selectedPaths.size());
                info.files << out;
                info.totalFileSize = QFileInfo(out).size();
                showProcessResult(info);
            });
        break;
    }
    case ToolType::Edit: {
        QImage img = m_editorWidget->renderedImage();
        if (img.isNull())
            break;
        const QString& path = paths.first();
        QFileInfo fi(path);
        QString fmt = fi.suffix();
        QString defaultPath =
            (lastBrowseDirectory().isEmpty() ? fi.absolutePath() : lastBrowseDirectory()) +
            QStringLiteral("/") + fi.completeBaseName() + QStringLiteral("_edited.") + fmt;
        QString filter = buildImageSaveFilter(fmt);
        QString out = QFileDialog::getSaveFileName(this, tr("Save Image"), defaultPath, filter);
        if (out.isEmpty())
            break;
        if (ImageLoader::saveImage(img, out, QString(), m_propertyPanel->editQuality())) {
            m_statusBar->setMessage(tr("Saved edited image: %1").arg(out));
            ResultInfo info;
            info.title = tr("Edit Saved");
            info.message = tr("Edited image saved");
            info.files << out;
            info.totalFileSize = QFileInfo(out).size();
            info.resolution = img.size();
            showProcessResult(info);
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

void MainWindow::showProcessResult(const ResultInfo& info)
{
    m_lastResult = info;
    if (m_lastResultBtn)
        m_lastResultBtn->setEnabled(true);

    // 完成反馈走轻提示：不打断操作流。「打开」直达系统查看器，
    // 「结果」打开结果清单 —— 结果信息一秒可达，不再只是记到日志里。
    ToastManager::instance().showInfo(info.message,
                                      ToastAction{tr("Open"),
                                                  [this]() {
                                                      if (!m_lastResult.files.isEmpty())
                                                          ExportManager::openFile(
                                                              m_lastResult.files.first());
                                                  }},
                                      this);
}

void MainWindow::onShowLastResult()
{
    if (m_lastResult.files.isEmpty())
        return;
    auto* dialog = new ResultDialog(m_lastResult, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
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
        m_statusBar->setMessage(
            tr("Batch: failed to create output directory %1").arg(batch.outputDir));
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

        m_statusBar->setMessage(
            tr("Batch saved %1 images to %2").arg(copied.size()).arg(batch.outputDir));
        if (!copied.isEmpty())
            ExportManager::showInFolder(copied.first());
    };

    switch (batch.targetTool) {
    case ToolType::Convert: {
        runEngineAsync<ConvertEngine>(
            m_propertyPanel->convertSettings(), paths, tr("Batch converting..."),
            [copyToBatchDir](const QStringList& outs) { copyToBatchDir(outs); }, true);
        break;
    }
    case ToolType::Compress: {
        runEngineAsync<CompressEngine>(
            m_propertyPanel->compressSettings(), paths, tr("Batch compressing..."),
            [copyToBatchDir](const QList<CompressResult>& results) {
                QStringList outs;
                for (const auto& r : results) {
                    if (r.success)
                        outs.append(r.outputPath);
                }
                copyToBatchDir(outs);
            },
            true);
        break;
    }
    case ToolType::Watermark: {
        runEngineAsync<WatermarkEngine>(
            m_propertyPanel->watermarkSettings(), paths, tr("Batch watermarking..."),
            [copyToBatchDir](const QStringList& outs) { copyToBatchDir(outs); }, true);
        break;
    }
    case ToolType::Resize: {
        runEngineAsync<ResizeEngine>(
            m_propertyPanel->resizeSettings(), paths, tr("Batch resizing..."),
            [copyToBatchDir](const QStringList& outs) { copyToBatchDir(outs); }, true);
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
    // 拼接模式下双击显示单张预览（applyToolEffect=false），其他模式正常更新预览。
    // 双击前的 toggle/回退会触发 imageSelectionChanged 启动延迟刷新，
    // 这里停止定时器，避免拼接预览在 200ms 后覆盖单图预览。
    if (m_currentTool == ToolType::Stitch && m_previewDelayTimer)
        m_previewDelayTimer->stop();
    updatePreview(m_currentTool != ToolType::Stitch);
}

void MainWindow::onImageSelected(int row)
{
    // 拼接 toggle 模式下，同一行的选中状态可能变化（双击回退后再次点击），
    // 不能因 row 未变就跳过刷新；其他模式 row 未变确实无需更新。
    if (row == m_currentImageRow && m_currentTool != ToolType::Stitch)
        return;
    m_currentImageRow = row;
    // 图片信息面板跟随选中（EXIF 异步读取，PropertyPanel 内部做过期丢弃）。
    const ImageItem* selItem =
        row >= 0 && row < m_model->rowCount() ? m_model->itemAt(row) : nullptr;
    m_propertyPanel->showImageInfo(selItem ? selItem->filePath() : QString());
    if (m_currentTool == ToolType::Stitch) {
        m_previewWidget->setStitchHighlightedInput(row);
        // 拼接预览由 imageSelectionChanged → 200ms 延迟定时器统一触发，
        // 这里不立即调用 updatePreview，否则会与定时器触发的 updateToolPreview
        // 重复执行：前一个任务被 cancel 后 finished 信号被抑制，
        // setOverrideCursor/restoreOverrideCursor 不配对，光标卡在忙碌状态。
        return;
    }
    // 非拼接模式显示单张原图预览。
    updatePreview(false);
}

void MainWindow::updatePreview(bool applyToolEffect)
{
    const bool hasImages = m_model->rowCount() > 0;
    if (!hasImages) {
        m_previewWidget->clear();
        return;
    }

    // 拼接模式下：applyToolEffect=true 显示选中图片拼接效果，
    // applyToolEffect=false（双击）显示单张预览。
    const bool showStitch = (m_currentTool == ToolType::Stitch) && applyToolEffect;

    if (showStitch && m_model->selectedFilePaths().isEmpty()) {
        m_previewWidget->clear();
        return;
    }

    const bool hasCurrentRow = m_currentImageRow >= 0 && m_currentImageRow < m_model->rowCount();
    const ImageItem* item = hasCurrentRow ? m_model->itemAt(m_currentImageRow) : nullptr;
    if (!showStitch && (!hasCurrentRow || !item || !item->isValid())) {
        m_previewWidget->clear();
        return;
    }

    // 预览任务防堆积：连续改动选择会连续投递任务。专用池单线程串行 +
    // 代数检查 —— 过期任务在开工瞬间即退出，不再排队执行数 GB 的拼接管线。
    m_previewWidget->setComparisonMode(false);

    PreviewTaskInput input;
    input.tool = m_currentTool;
    const qint64 generation = ++m_previewGeneration;
    input.generation = generation;
    input.currentFilePath = item ? item->filePath() : QString();
    // 拼接预览仅使用选中的图片，其他模式使用全部可见图片。
    input.allFilePaths = showStitch ? m_model->selectedFilePaths() : m_model->filePaths();
    input.previewSize = m_previewWidget->viewportSize();
    if (input.previewSize.isEmpty())
        input.previewSize = QSize(1280, 720);
    // 限制预览 oversampling，避免大窗口下生成远超屏幕的巨型缓冲。
    input.previewSize *= 2;
    constexpr int kMaxPreviewLongEdge = 3840;
    if (qMax(input.previewSize.width(), input.previewSize.height()) > kMaxPreviewLongEdge) {
        input.previewSize =
            input.previewSize.scaled(kMaxPreviewLongEdge, kMaxPreviewLongEdge, Qt::KeepAspectRatio);
    }
    input.applyToolEffect = applyToolEffect;
    input.forceOriginal = m_compareOriginal;
    input.stitchSettings = m_propertyPanel->stitchSettings();
    input.compressSettings = m_propertyPanel->compressSettings();
    input.watermarkSettings = m_propertyPanel->watermarkSettings();
    input.resizeSettings = m_propertyPanel->resizeSettings();

    bool needsProgress =
        applyToolEffect &&
        (m_currentTool == ToolType::Stitch || m_currentTool == ToolType::Compress ||
         m_currentTool == ToolType::Watermark || m_currentTool == ToolType::Resize);
    if (needsProgress) {
        // 用标志位避免重复 push WaitCursor：当前任务被 cancel 时 finished 信号
        // 被抑制，restoreOverrideCursor 不会执行；若再次 setOverrideCursor 会导致
        // 栈中残留多个 WaitCursor，光标永久卡在忙碌状态。
        if (!m_previewCursorOverridden) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            m_previewCursorOverridden = true;
        }
        m_statusBar->setState(StatusBarWidget::State::Processing);
        m_statusBar->setMessage(tr("Updating preview..."));
    }

    QFuture<PreviewTaskResult> future =
        QtConcurrent::run(ImageTaskPools::previewPool(), [this, input]() -> PreviewTaskResult {
            // 代数检查：开工瞬间发现已被更新的请求取代，直接退出。
            // 空结果带自身代数，onPreviewFinished 据此识别并跳过。
            if (input.generation != m_previewGeneration.load()) {
                PreviewTaskResult stale;
                stale.generation = input.generation;
                return stale;
            }
            return generatePreview(input);
        });
    m_previewWatcher->setFuture(future);
}

void MainWindow::updateToolPreview()
{
    updatePreview(true);
}

void MainWindow::updateStatusBar()
{
    const int rowCount = m_model->rowCount();
    m_statusBar->setImageCount(rowCount, m_model->selectedCount());

    // 一次遍历同时累计总大小并收集尺寸：原先这里有多趟 O(n) 遍历
    // （rowCount/selectedCount/selectedFilePaths 各一次），图片多时白白浪费。
    qint64 totalSize = 0;
    QList<QSize> allSizes;
    QList<QSize> selectedSizes;
    allSizes.reserve(rowCount);
    for (int i = 0; i < rowCount; ++i) {
        const ImageItem* item = m_model->itemAt(i);
        if (!item)
            continue;
        totalSize += item->fileSize();
        if (!item->isValid())
            continue;
        const QSize size(item->width(), item->height());
        allSizes.append(size);
        if (item->isSelected() && !item->isHidden())
            selectedSizes.append(size);
    }
    m_statusBar->setTotalFileSize(totalSize);

    QSize outputSize;
    if (m_currentTool == ToolType::Stitch) {
        // 拼接输出尺寸：用**已缓存的图片尺寸**做纯内存估算。
        // 原先调用按路径的estimateOutputSize，会逐个打开文件读图片头 ——
        // 在主线程上对上百张图执行，每次选择变化都卡一下。
        const QList<QSize>& used = selectedSizes.isEmpty() ? allSizes : selectedSizes;
        if (!used.isEmpty()) {
            const StitchSettings settings = m_propertyPanel->stitchSettings();
            outputSize = StitchEngine::estimateOutputSize(used, settings);
        }
    } else if (m_currentImageRow >= 0 && m_currentImageRow < rowCount) {
        const ImageItem* item = m_model->itemAt(m_currentImageRow);
        if (item && item->isValid())
            outputSize = QSize(item->width(), item->height());
    }

    // 只设置一次。原先拼接分支设置完尺寸后又无条件 setOutputSize(outputSize)
    // 覆盖了一遍，而此时 outputSize 为空 → 标签被隐藏，拼接输出尺寸永远显示不出来。
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
    // 加载会话结束：冲刷挂起的状态栏刷新（防抖定时器可能还没到期），
    // 保证最后一批尺寸立即反映在输出尺寸上。
    if (m_statusBarRefreshTimer->isActive()) {
        m_statusBarRefreshTimer->stop();
        updateStatusBar();
    }
}

void MainWindow::showErrorToastCoalesced(const ImageProError& error)
{
    // 错误突发合并：100 个坏文件逐条 Toast 会把屏幕刷满。
    // 1 秒窗口内的同类错误只弹第一条，其余计数；窗口结束后补一条汇总。
    const bool burst = m_errorToastClockValid && m_lastToastLevel == error.level() &&
                       m_errorToastClock.elapsed() < 1000;
    if (!burst) {
        flushSuppressedErrors();
        m_lastToastLevel = error.level();
        m_errorToastClockValid = true;
        m_errorToastClock.restart();
        m_suppressedToasts = 0;
        ToastManager::instance().show(error.level(), error.message(), this);
        m_errorBurstTimer->start();
        return;
    }
    ++m_suppressedToasts;
    m_errorBurstTimer->start(); // 突发还在继续，窗口顺延
}

void MainWindow::flushSuppressedErrors()
{
    if (m_suppressedToasts <= 0)
        return;
    ToastManager::instance().showInfo(
        tr("%1 similar notifications were grouped").arg(m_suppressedToasts), this);
    m_suppressedToasts = 0;
}

void MainWindow::onPreviewFinished()
{
    PreviewTaskResult result = m_previewWatcher->result();

    // 过期结果（更新的请求已接管）：跳过渲染。WaitCursor 由最新任务的
    // finished 恢复，这里不动光标状态。
    if (result.generation != 0 && result.generation != m_previewGeneration.load())
        return;

    // 仅在曾经设置过 WaitCursor 时恢复，保证与 updatePreview 中的 set 配对。
    if (m_previewCursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_previewCursorOverridden = false;
    }
    m_statusBar->setState(StatusBarWidget::State::Ready);
    m_statusBar->setMessage(tr("Ready"));

    if (m_currentTool == ToolType::Edit) {
        m_editorWidget->setDocumentImage(result.image, result.sourcePath);
        return;
    }

    if (m_currentTool == ToolType::Stitch) {
        m_previewWidget->setComparisonMode(false);
        m_previewWidget->setStitchSynthesizedImage(result.image);
        m_previewWidget->setStitchInputRects(result.stitchInputRects);
        m_propertyPanel->updatePreview(result.image, result.sourceSize);
        return;
    }

    if (result.comparisonMode && !result.originalImage.isNull()) {
        m_previewWidget->setOriginalImage(result.originalImage);
        m_previewWidget->setComparisonMode(true);
    } else {
        m_previewWidget->setComparisonMode(false);
    }

    if (!result.sourcePath.isEmpty()) {
        m_previewWidget->setSourcePath(result.sourcePath, result.sourceSize, result.sourceRotation,
                                       result.sourceFlippedH, result.sourceFlippedV);
    } else {
        m_previewWidget->setImage(result.image);
    }
    m_propertyPanel->updatePreview(result.image, result.sourceSize);
}

void MainWindow::requestDelayedPreview()
{
    if (m_previewDelayTimer)
        m_previewDelayTimer->start();
}

void MainWindow::toggleFullScreen()
{
    // F11 / Space / Esc 循环：进入全屏记住是否原为最大化，退出时还原。
    if (isFullScreen()) {
        if (m_wasMaximizedBeforeFullScreen)
            showMaximized();
        else
            showNormal();
    } else {
        m_wasMaximizedBeforeFullScreen = isMaximized();
        showFullScreen();
    }
}

void MainWindow::onNextImage()
{
    const int count = m_model->rowCount();
    if (count == 0)
        return;
    const int row = (m_currentImageRow + 1 + count) % count;
    m_listWidget->selectRow(row);
    onImageSelected(row);
}

void MainWindow::onPreviousImage()
{
    const int count = m_model->rowCount();
    if (count == 0)
        return;
    const int row = (m_currentImageRow - 1 + count) % count;
    m_listWidget->selectRow(row);
    onImageSelected(row);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    // Esc 退出全屏（全屏模式下无论焦点在哪都响应）。
    if (isFullScreen() && event->key() == Qt::Key_Escape) {
        toggleFullScreen();
        return;
    }

    // 裸键快捷键：文本输入框聚焦时不拦截（面板里有文件名模板/质量等输入框），
    // 否则 Space/←/→/L/R/H/V 全窗口生效。
    const QWidget* focused = QApplication::focusWidget();
    const bool textFocused = focused && (qobject_cast<const QLineEdit*>(focused) ||
                                         qobject_cast<const QTextEdit*>(focused) ||
                                         qobject_cast<const QPlainTextEdit*>(focused) ||
                                         (qobject_cast<const QComboBox*>(focused) &&
                                          qobject_cast<const QComboBox*>(focused)->isEditable()));
    if (!textFocused) {
        switch (event->key()) {
        case Qt::Key_Space:
            toggleFullScreen();
            return;
        case Qt::Key_Right:
            onNextImage();
            return;
        case Qt::Key_Left:
            onPreviousImage();
            return;
        case Qt::Key_L:
            onPreviewRotateLeft();
            return;
        case Qt::Key_R:
            onPreviewRotateRight();
            return;
        case Qt::Key_H:
            onPreviewFlipHorizontal();
            return;
        case Qt::Key_V:
            onPreviewFlipVertical();
            return;
        default:
            break;
        }
    }
    QMainWindow::keyPressEvent(event);
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

void MainWindow::bringToFront()
{
    if (isMinimized())
        showNormal();
    else
        show();

    raise();
    activateWindow();

#ifdef Q_OS_WIN
    // Windows 的前台窗口锁会阻止后台进程抢焦点：仅 activateWindow() 常常无效，
    // 必须显式调用 SetForegroundWindow 才能真正把窗口提到最前。
    // winId() 返回的就是 WId（无符号指针宽度整数），这是 Win32 句柄转换的固有写法。
    // NOLINTNEXTLINE
    ::SetForegroundWindow(reinterpret_cast<HWND>(winId()));
#endif
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());

    // 停止尚未触发的延迟预览，避免关闭过程中又启动新的后台任务。
    if (m_previewDelayTimer)
        m_previewDelayTimer->stop();

    // 关闭进度对话框：否则它会无主地留在屏幕上，而进程要等全局线程池排空
    // 才真正退出，用户会以为程序卡死。
    // 注意：各引擎基于 QtConcurrent::run，无法中途取消，这里只能清理 UI，
    // 无法缩短退出等待。
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    event->accept();
}

} // namespace yingtu
