#pragma once

#include "ToolBarWidget.h"
#include "core/StitchEngine.h"
#include "core/ConvertEngine.h"
#include "core/CompressEngine.h"
#include "core/WatermarkEngine.h"
#include "core/ResizeEngine.h"
#include "utils/EditAction.h"
#include "utils/ResizePresetManager.h"
#include "utils/ResizeSettings.h"
#include "utils/StitchPreset.h"
#include <QWidget>

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
class QTimer;
class QToolButton;

namespace yingtu {

class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    void setToolType(ToolType tool);

    StitchSettings stitchSettings() const;
    ConvertSettings convertSettings() const;
    CompressSettings compressSettings() const;
    WatermarkSettings watermarkSettings() const;
    EditAction currentEditAction() const;
    ResizeSettings resizeSettings() const;

    struct BatchSettings {
        ToolType targetTool = ToolType::Convert;
        QString outputDir;
    };
    BatchSettings batchSettings() const;

    void updatePreview(const QImage& image);

signals:
    void settingsChanged();
    void previewRequested();
    void processRequested();
    void editUndoRequested();
    void editRedoRequested();
    void editClearRequested();
    void editHistoryJumpRequested(int index);

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

    void onStitchPresetComboChanged(int index);
    void onStitchAddPreset();
    void onStitchBgColorClicked();

    void onConvertEstimateTimeout();
    void onConvertFormatChanged(int index);

    void onCompressModeChanged(int index);
    void onCompressEstimateRequested();

    void onWatermarkColorClicked();
    void onWatermarkTypeChanged(int index);

    void onEditToolChanged(int index);
    void onEditUndo();
    void onEditRedo();
    void onEditClear();
    void onEditHistoryItemClicked(QListWidgetItem* item);
    void onEditActionAdded(const EditAction& action);

    void onResizeOutputFormatChanged(int index);

public slots:
    void refreshEditHistory(const QList<EditAction>& history, int currentIndex);

private:
    void buildStitchPanel();
    void buildConvertPanel();
    void buildCompressPanel();
    void buildWatermarkPanel();
    void buildEditPanel();
    void buildResizePanel();
    void buildBatchPanel();

    QWidget* createFormRow(const QString& label, QWidget* widget);
    QWidget* createFormRow(const QString& label, QWidget* widget, QWidget*& rowStorage);

    void loadResizePresetCategories();
    void loadResizePresetsByCategory();
    void applyResizePreset(const ResizePreset& preset);
    QString aspectRatioString(int w, int h) const;

    void loadStitchPresets();
    void applyStitchPreset(const StitchPreset& preset);

    QStackedWidget* m_stack = nullptr;

    // Stitch
    QComboBox* m_stitchDirection = nullptr;
    QSpinBox* m_stitchSpacing = nullptr;
    QComboBox* m_stitchBackground = nullptr;
    QPushButton* m_stitchBgColorBtn = nullptr;
    QLabel* m_stitchBgColorLabel = nullptr;
    QCheckBox* m_stitchUniformWidth = nullptr;
    QCheckBox* m_stitchRemoveWhiteEdges = nullptr;
    QCheckBox* m_stitchAutoCropEdges = nullptr;
    QComboBox* m_stitchPresetCombo = nullptr;
    QPushButton* m_stitchAddPresetBtn = nullptr;
    QList<StitchPreset> m_stitchPresets;
    QSpinBox* m_stitchGridRows = nullptr;
    QSpinBox* m_stitchGridColumns = nullptr;
    QComboBox* m_stitchOutputFormat = nullptr;
    QSlider* m_stitchQuality = nullptr;

    // Convert
    QComboBox* m_convertFormat = nullptr;
    QSlider* m_convertQuality = nullptr;
    QWidget* m_convertQualityRow = nullptr;
    QCheckBox* m_convertKeepExif = nullptr;
    QCheckBox* m_convertToSRgb = nullptr;
    QLabel* m_convertEstimateLabel = nullptr;
    QTimer* m_convertEstimateTimer = nullptr;

    // Compress
    QComboBox* m_compressMode = nullptr;
    QSlider* m_compressStrength = nullptr;
    QSlider* m_compressQuality = nullptr;
    QSpinBox* m_compressScale = nullptr;
    QDoubleSpinBox* m_compressTargetSize = nullptr;
    QComboBox* m_compressTargetUnit = nullptr;
    QLabel* m_compressEstimateLabel = nullptr;
    QCheckBox* m_compressShowOriginal = nullptr;

    // Watermark
    QComboBox* m_watermarkType = nullptr;
    QLineEdit* m_watermarkText = nullptr;
    QLineEdit* m_watermarkImagePath = nullptr;
    QComboBox* m_watermarkFontFamily = nullptr;
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
    QLineEdit* m_watermarkOutputDir = nullptr;

    // Edit
    QComboBox* m_editTool = nullptr;
    QComboBox* m_editColor = nullptr;
    QSpinBox* m_editLineWidth = nullptr;
    QSlider* m_editOpacity = nullptr;
    QSpinBox* m_editFontSize = nullptr;
    QComboBox* m_editFillStyle = nullptr;
    QListWidget* m_editHistoryList = nullptr;
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
    QLineEdit* m_resizeOutputDir = nullptr;
    QTimer* m_resizeDelayTimer = nullptr;
    QList<ResizePreset> m_resizePresets;
    bool m_resizeUpdating = false;

    // Batch
    QComboBox* m_batchTargetTool = nullptr;
    QLineEdit* m_batchOutputDir = nullptr;

    QImage m_lastImage;
};

} // namespace yingtu
