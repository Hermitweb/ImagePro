#pragma once

#include "CollapsibleSection.h"
#include "ToolBarWidget.h"
#include "core/StitchEngine.h"
#include "core/ConvertEngine.h"
#include "core/CompressEngine.h"
#include "core/WatermarkEngine.h"
#include "core/ResizeEngine.h"
#include "core/PdfEngine.h"
#include "utils/EditAction.h"
#include "utils/ResizePresetManager.h"
#include "utils/ResizeSettings.h"
#include "utils/StitchPreset.h"
#include <QWidget>

#include <atomic>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QColorDialog;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QScrollArea;
class QTimer;
class QToolButton;

namespace yingtu {

class ImageListModel;
class OutputSettingsSection;

class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget* parent = nullptr);
    ~PropertyPanel() override;

    void setToolType(ToolType tool);
    void setImageModel(ImageListModel* model);

    StitchSettings stitchSettings() const;
    ConvertSettings convertSettings() const;
    CompressSettings compressSettings() const;
    WatermarkSettings watermarkSettings() const;
    EditAction currentEditAction() const;
    /** 编辑结果的保存质量（编辑面板可调）。 */
    int editQuality() const;
    ResizeSettings resizeSettings() const;
    PdfSettings pdfSettings() const;
    QString outputFileNameTemplate() const;
    /** 最近使用的输出目录（跨工具共享历史的第一项；无历史时为空串）。 */
    QString recentOutputDirectory() const;

    /**
     * 显示当前选中图片的信息（EXIF 异步读取，过期请求自动丢弃）。
     * 空路径清空显示。
     */
    void showImageInfo(const QString& filePath);

    struct BatchSettings {
        ToolType targetTool = ToolType::Convert;
        QString outputDir;
    };
    BatchSettings batchSettings() const;

    void updatePreview(const QImage& image, const QSize& sourceSize = QSize());
    /**
     * 刷新拼接输出文件名的预览标签。
     * 完整文本放进 tooltip，标签内按可用宽度做中间省略。
     */
    void updateStitchFileNamePreview();
    /**
     * 语言切换：重建各工具面板（文本全部由 tr() 产生），并把用户已填的
     * 控件取值原样还原，避免切换语言把参数清空。
     */
    void retranslateUi();
    /** 重新创建八个工具面板。 */
    void rebuildPanels();
signals:
    void settingsChanged();
    void previewRequested();
    void processRequested();
    /** 裁剪比例锁状态变化（MainWindow 转发给编辑器）。 */
    void cropAspectLockChanged(bool locked);
    void editUndoRequested();
    void editRedoRequested();
    void editClearRequested();
    void editHistoryJumpRequested(int index);
    void stitchSelectAllRequested();
    void stitchClearSelectionRequested();
    void stitchInvertSelectionRequested();

private slots:
    void onSettingsChanged();
    void onResizeModeChanged(int index);
    void onResizeCategoryChanged(int index);
    void onResizePresetChanged(int index);
    void onResizeWidthChanged(int value);
    void onResizeHeightChanged(int value);
    void onResizeAddPreset();
    void onResizeDeletePreset();
    void requestDelayedPreview();

    void onStitchCategoryChanged(int index);
    void onStitchPresetChanged(int index);
    void onStitchAddPreset();
    void onStitchDeletePreset();
    void onStitchBgColorClicked();
    void onStitchOutputDirChanged();
    void onStitchFileNameTemplateChanged();
    void onStitchOutputFormatChanged(int index);
    void onStitchDirectionChanged(int index);
    void onStitchBackgroundChanged(int index);

    void onConvertEstimateTimeout();
    void onConvertFormatChanged(int index);

    void onCompressModeChanged(int index);
    void onCompressEstimateRequested();

    void onWatermarkColorClicked();
    void onWatermarkTypeChanged(int index);

    void onEditToolChanged(int index);
    void onEditFontColorClicked();
    void onEditUndo();
    void onEditRedo();
    void onEditClear();
    void onEditHistoryItemClicked(QListWidgetItem* item);
    void onEditActionAdded(const EditAction& action);

    void onResizeOutputFormatChanged(int index);

    void onImageCountChanged(int count);
    void validateAndUpdateUI();

public slots:
    void refreshEditHistory(const QList<EditAction>& history, int currentIndex);

protected:
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    // 语言切换：重建各工具面板并还原用户已填的值。
    void changeEvent(QEvent* event) override;

private:
    void buildStitchPanel();
    void buildConvertPanel();
    void buildCompressPanel();
    void buildWatermarkPanel();
    void buildEditPanel();
    void buildResizePanel();
    void buildBatchPanel();
    void buildPdfPanel();

    QWidget* createFormRow(const QString& label, QWidget* widget);
    QWidget* createFormRow(const QString& label, QWidget* widget, QWidget*& rowStorage);

    /** 主按钮的文案随工具变化（开始拼接/开始转换/…）。 */
    QString actionButtonText(ToolType tool) const;
    /** 批处理参数摘要：实时显示目标工具面板将应用的参数。 */
    void updateBatchSummary();

    void loadResizePresetCategories();
    void loadResizePresetsByCategory();
    void applyResizePreset(const ResizePreset& preset);
    QString aspectRatioString(int w, int h) const;

    void loadStitchPresets();
    void applyStitchPreset(const StitchPreset& preset);
    void selectStitchPresetById(const QString& presetId);
    void clearStitchPresetSelection();
    void updateStitchPresetButtons();
    void refreshStitchPresetAvailability();
    void rebuildStitchPresetDropdown();
    QString stitchPresetDisplayText(const StitchPreset& preset) const;
    void updateStitchControlVisibility();

    void addOutputHistory(const QString& path);
    void saveUiState();
    void loadUiState();
    QString uiStateFilePath() const;

    bool validateStitchSettings(QString* errorMessage = nullptr,
                                QString* warningMessage = nullptr) const;
    bool canWriteToPath(const QString& path) const;
    void setInputError(QWidget* widget, bool error);
    void setInputWarning(QWidget* widget, bool warning);
    QString previewFileNameFromTemplate(const QString& templ) const;

    QStackedWidget* m_stack = nullptr;
    // 设置区的滚动容器：窗口变矮时内容可滚动，而不是被裁掉。
    QScrollArea* m_scrollArea = nullptr;
    QPushButton* m_processBtn = nullptr;

    // Stitch
    CollapsibleSection* m_stitchSectionSettings = nullptr;
    CollapsibleSection* m_stitchSectionPresets = nullptr;
    CollapsibleSection* m_stitchSectionOutput = nullptr;

    QComboBox* m_stitchDirection = nullptr;
    QSpinBox* m_stitchSpacing = nullptr;
    QComboBox* m_stitchBackground = nullptr;
    QPushButton* m_stitchBgColorBtn = nullptr;
    QLabel* m_stitchBgColorLabel = nullptr;
    QWidget* m_stitchBgColorRowWidget = nullptr;
    QCheckBox* m_stitchUniformWidth = nullptr;
    QCheckBox* m_stitchRemoveWhiteEdges = nullptr;
    QCheckBox* m_stitchAutoCropEdges = nullptr;
    QSpinBox* m_stitchGridRows = nullptr;
    QSpinBox* m_stitchGridColumns = nullptr;
    QWidget* m_stitchGridRowWidget = nullptr;
    QComboBox* m_stitchOutputFormat = nullptr;
    QSlider* m_stitchQuality = nullptr;
    QPushButton* m_stitchBatchBtn = nullptr;

    QComboBox* m_stitchPresetCategory = nullptr;
    QComboBox* m_stitchPreset = nullptr;
    QPushButton* m_stitchPresetDeleteBtn = nullptr;
    QList<StitchPreset> m_stitchPresets;
    QString m_currentStitchPresetId;
    bool m_applyingStitchPreset = false;
    bool m_rebuildingStitchPresets = false;

    // 拼接输出段（目录+模板+校验统一放在 OutputSettingsSection 里）
    OutputSettingsSection* m_stitchOutputSection = nullptr;

    // Convert
    QComboBox* m_convertFormat = nullptr;
    QSlider* m_convertQuality = nullptr;
    QWidget* m_convertQualityRow = nullptr;
    QCheckBox* m_convertKeepExif = nullptr;
    QCheckBox* m_convertToSRgb = nullptr;
    QLabel* m_convertEstimateLabel = nullptr;
    QTimer* m_convertEstimateTimer = nullptr;
    OutputSettingsSection* m_convertOutputSection = nullptr;

    // Compress
    QComboBox* m_compressMode = nullptr;
    QSlider* m_compressStrength = nullptr;
    QSlider* m_compressQuality = nullptr;
    QSpinBox* m_compressScale = nullptr;
    QDoubleSpinBox* m_compressTargetSize = nullptr;
    QComboBox* m_compressTargetUnit = nullptr;
    QLabel* m_compressEstimateLabel = nullptr;
    QCheckBox* m_compressShowOriginal = nullptr;
    OutputSettingsSection* m_compressOutputSection = nullptr;
    QComboBox* m_compressOutputFormat = nullptr;

    // Watermark
    QComboBox* m_watermarkType = nullptr;
    QLineEdit* m_watermarkText = nullptr;
    QLineEdit* m_watermarkImagePath = nullptr;
    QSpinBox* m_watermarkImageScale = nullptr;    QComboBox* m_watermarkFontFamily = nullptr;
    QSpinBox* m_watermarkFontSize = nullptr;
    QPushButton* m_watermarkColorBtn = nullptr;
    QColor m_watermarkColor = Qt::white;
    QSlider* m_watermarkOpacity = nullptr;
    QSpinBox* m_watermarkRotation = nullptr;
    QComboBox* m_watermarkPosition = nullptr;
    QCheckBox* m_watermarkTile = nullptr;
    QSpinBox* m_watermarkTileSpacing = nullptr;
    QSpinBox* m_watermarkMargin = nullptr;
    QComboBox* m_watermarkOutputFormat = nullptr;
    QSlider* m_watermarkQuality = nullptr;
    OutputSettingsSection* m_watermarkOutputSection = nullptr;

    // Edit
    QComboBox* m_editTool = nullptr;
    QComboBox* m_editFilterType = nullptr;
    QSlider* m_editFilterStrength = nullptr;
    QComboBox* m_editMosaicStyle = nullptr;
    QSpinBox* m_editMosaicSize = nullptr;
    QComboBox* m_editColor = nullptr;
    QSpinBox* m_editLineWidth = nullptr;
    QSlider* m_editOpacity = nullptr;
    QComboBox* m_editFontFamily = nullptr;
    QSpinBox* m_editFontSize = nullptr;
    QCheckBox* m_editFontBold = nullptr;
    QCheckBox* m_editTextBackground = nullptr;
    QCheckBox* m_editCropAspectLock = nullptr;
    QPushButton* m_editTextColorBtn = nullptr;
    QColor m_editTextColor = Qt::red;
    QComboBox* m_editFillStyle = nullptr;
    QListWidget* m_editHistoryList = nullptr;
    QSpinBox* m_editQuality = nullptr;
    QPushButton* m_editUndoBtn = nullptr;
    QPushButton* m_editRedoBtn = nullptr;
    QPushButton* m_editClearBtn = nullptr;
    int m_editHistoryIndex = -1;

    // Resize
    QComboBox* m_resizeMode = nullptr;
    QSpinBox* m_resizePercentage = nullptr;
    QSpinBox* m_resizeWidth = nullptr;
    QSpinBox* m_resizeHeight = nullptr;
    QComboBox* m_resizeInterpolation = nullptr;
    QComboBox* m_resizeCategory = nullptr;
    QComboBox* m_resizePreset = nullptr;
    QLabel* m_resizeAspectLabel = nullptr;
    QToolButton* m_resizeLockAspect = nullptr;
    QCheckBox* m_resizeFitWithinOriginal = nullptr;
    QPushButton* m_resizeAddPresetBtn = nullptr;
    QPushButton* m_resizeDeletePresetBtn = nullptr;
    QComboBox* m_resizeOutputFormat = nullptr;
    QSlider* m_resizeQuality = nullptr;
    OutputSettingsSection* m_resizeOutputSection = nullptr;
    QTimer* m_resizeDelayTimer = nullptr;
    QList<ResizePreset> m_resizePresets;
    bool m_resizeUpdating = false;

    // Batch
    QComboBox* m_batchTargetTool = nullptr;
    OutputSettingsSection* m_batchOutputSection = nullptr;
    QLabel* m_batchSummary = nullptr;

    // PDF
    QComboBox* m_pdfPageSize = nullptr;
    QWidget* m_pdfCustomSizeRow = nullptr;
    QDoubleSpinBox* m_pdfCustomWidth = nullptr;
    QDoubleSpinBox* m_pdfCustomHeight = nullptr;
    QComboBox* m_pdfLayout = nullptr;
    QSpinBox* m_pdfDpi = nullptr;
    QDoubleSpinBox* m_pdfMarginLeft = nullptr;
    QDoubleSpinBox* m_pdfMarginTop = nullptr;
    QDoubleSpinBox* m_pdfMarginRight = nullptr;
    QDoubleSpinBox* m_pdfMarginBottom = nullptr;
    QLineEdit* m_pdfOutputPath = nullptr;

    // Shared
    QImage m_lastImage;
    QSize m_lastSourceSize; // 当前预览对应的原图尺寸（估算用，预览图被降采样过）
    ImageListModel* m_imageModel = nullptr;
    // 图片信息段（EXIF 异步读取）
    CollapsibleSection* m_infoSection = nullptr;
    QLabel* m_infoText = nullptr;
    QString m_infoPath;
    std::atomic<int> m_infoGeneration{0};
    QStringList m_outputHistory;
    // 全部工具的输出段：最近目录历史跨工具共享，任一处选择都对其它面板可见。
    QList<OutputSettingsSection*> m_outputSections;
    bool m_validationEnabled = true;
    bool m_initializing = true;
    bool m_validating = false;
};

} // namespace yingtu
