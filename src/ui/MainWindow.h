#pragma once

#include "ToolBarWidget.h"
#include "core/ImageListModel.h"
#include "dialogs/ResultDialog.h"
#include "utils/ErrorLevel.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QProgressDialog>
#include <QSize>
#include <QTimer>
#include <atomic>
#include <functional>
#include <utility>

class QSplitter;
class QLabel;
class QProgressDialog;
class QStackedWidget;
class QToolButton;

namespace yingtu {

class ImageProError;
class ImageListWidget;
class PreviewWidget;
class PropertyPanel;
class StatusBarWidget;
class ToolBarWidget;
class ImageEditorWidget;
class FlowLayout;

/**
 * 引擎 process() 的返回类型。
 *
 * 抽成别名而不是在参数列表里直接写嵌套的 decltype：
 * 后者在模板参数里嵌套两层 decltype 时，某些编译器会解析失败，
 * 且可读性很差。
 */
template <typename EngineType>
using EngineResultOf = decltype(std::declval<EngineType>().process(std::declval<QStringList>()));

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * 窗口内部持有的图片列表模型。
     *
     * 供测试与外部代码（如命令导入）访问；返回的是窗口自有模型，
     * 调用方不应替换或删除其中的条目。
     */
    ImageListModel* imageModel() const { return m_model; }
    /**
     * 把窗口提到前台。用于「已有实例收到新实例的激活请求」场景。
     *
     * 单独抽成公开方法而不是在调用点拼 show/raise/activateWindow，
     * 是因为 Windows 的前台窗口锁：后台进程仅靠 activateWindow()
     * 常常抢不到焦点，必须额外调用 SetForegroundWindow。
     */
    void bringToFront();

protected:
    void closeEvent(QCloseEvent* event) override;
    // 语言切换：Qt 在更换翻译器后向所有控件投递 LanguageChange，
    // 这里据此重新套用界面文本，实现「无需重启」切换。
    void changeEvent(QEvent* event) override;

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralWidget();
    void setupStatusBar();
    void setupPreviewToolBar();
    void connectSignals();

    /** 重新套用界面文本（语言切换时由 LanguageChange 事件触发）。 */
    void retranslateUi();
    /** 重建菜单栏：菜单项与动作较多，整体重建比逐条维护对应关系更可靠。 */
    void rebuildMenuBar();
    /** 重建预览工具栏上的按钮。 */
    void rebuildPreviewToolBar();
    /** 创建预览工具栏上的各个按钮。 */
    void populatePreviewToolBar();

    void onAddImages();
    void onRemoveImage();
    void onAddFolder();
    void onPasteImage();
    /** 文件对话框的起始目录：优先取跨工具共享的输出目录历史。 */
    QString lastBrowseDirectory() const;
    void rememberRecentFiles(const QStringList& files);
    void rebuildRecentFilesMenu();
    // 移除指定行并修正当前行/刷新预览。工具栏「移除」与列表的
    // removeRequested 信号都走这里，避免两处逻辑分叉。
    void removeImages(const QModelIndexList& indexes);
    void onClearImages();
    void onToolChanged(ToolType tool);
    void onPreviewRequested();
    void onProcessRequested();
    void onBatchProcess();

    // 将引擎处理移到后台线程，避免主线程被批量处理阻塞
    template <typename EngineType, typename SettingsType>
    void runEngineAsync(const SettingsType& settings, const QStringList& paths,
                        const QString& progressTitle,
                        const std::function<void(const EngineResultOf<EngineType>&)>& onFinished,
                        bool showProgressDialog = true);

    void onImageDoubleClicked(int row);
    void onImageSelected(int row);
    void updatePreview(bool applyToolEffect = false);
    void updateToolPreview();
    void updateStatusBar();

    /**
     * 处理完成的统一出口：记录结果、点亮「结果」按钮、发轻提示。
     * 不再弹模态对话框打断操作流 —— 结果列表收进「结果」入口，需要时再看。
     */
    void showProcessResult(const ResultInfo& info);
    /** 以非模态窗口展示最近一次处理结果。 */
    void onShowLastResult();
    /** 错误 Toast 合并：突发窗口内的同类错误只弹第一条（见 showErrorToastCoalesced）。 */
    void showErrorToastCoalesced(const ImageProError& error);
    /** 突发结束后补一条「另有 N 条同类提示」的汇总。 */
    void flushSuppressedErrors();

    // 快捷键体系
    void toggleFullScreen();
    void onNextImage();
    void onPreviousImage();

protected:
    void keyPressEvent(QKeyEvent* event) override;

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
    // 预览工具栏的布局：语言切换时清空并重建其中的按钮。
    FlowLayout* m_previewToolBarLayout = nullptr;
    QLabel* m_zoomLabel = nullptr;
    // 预览工具栏上的「结果」按钮：最近一次处理完成后点亮。
    QToolButton* m_lastResultBtn = nullptr;
    ResultInfo m_lastResult;
    // 预览工具栏上的「对比」开关：处理效果与原图并排。
    QToolButton* m_compareBtn = nullptr;
    bool m_compareOriginal = false;
    ImageEditorWidget* m_editorWidget = nullptr;
    QStackedWidget* m_centerStack = nullptr;

    ToolType m_currentTool = ToolType::Stitch;
    int m_currentImageRow = -1;
    QTimer* m_previewDelayTimer = nullptr;
    // 预览任务代数：每次 updatePreview 递增，过期任务开工即退（防数 GB 任务排队堆积）。
    std::atomic<qint64> m_previewGeneration{0};
    // 状态栏刷新防抖：批量加载时尺寸逐张到达，合并 200ms 内的刷新。
    QTimer* m_statusBarRefreshTimer = nullptr;
    QMenu* m_recentMenu = nullptr;
    QPointer<QFutureWatcher<struct PreviewTaskResult>> m_previewWatcher;
    QPointer<QProgressDialog> m_progressDialog;

    // 错误 Toast 合并状态（GUI 线程专用）。
    QElapsedTimer m_errorToastClock;
    bool m_errorToastClockValid = false;
    ErrorLevel m_lastToastLevel = ErrorLevel::Error;
    int m_suppressedToasts = 0;
    QTimer* m_errorBurstTimer = nullptr;
    bool m_wasMaximizedBeforeFullScreen = false;

    // true 表示当前更新检查为后台静默检查（仅 toast，不弹模态对话框）。
    bool m_silentUpdateCheck = false;
    // 用户已手动触发过检查则不再执行启动后台检查，避免并发竞态。
    bool m_manualUpdateRequested = false;

    // 标记当前是否已为预览任务设置 WaitCursor。
    // updatePreview 可能被多次调用（立即 + 200ms 延迟），若前一个任务被 cancel，
    // 其 finished 信号被抑制，restoreOverrideCursor 不会执行，
    // 导致 override cursor 栈泄漏、光标卡在忙碌状态。用标志位确保配对。
    bool m_previewCursorOverridden = false;
};

} // namespace yingtu
