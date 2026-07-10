#include "PropertyPanel.h"
#include "core/ImageListModel.h"
#include "utils/FileUtils.h"
#include "utils/ResizePresetManager.h"
#include "utils/StitchPresetManager.h"
#include <QAbstractButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QStandardItemModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

namespace yingtu {

PropertyPanel::PropertyPanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(200);
    setMaximumWidth(260);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(8);

    m_stack = new QStackedWidget(this);
    buildStitchPanel();
    buildConvertPanel();
    buildCompressPanel();
    buildWatermarkPanel();
    buildEditPanel();
    buildResizePanel();
    buildBatchPanel();
    buildPdfPanel();

    mainLayout->addWidget(m_stack, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);
    m_previewBtn = new QPushButton(tr("Preview"), this);
    m_processBtn = new QPushButton(tr("Start"), this);
    m_processBtn->setDefault(true);
    btnLayout->addWidget(m_previewBtn);
    btnLayout->addWidget(m_processBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_previewBtn, &QPushButton::clicked, this, &PropertyPanel::previewRequested);
    connect(m_processBtn, &QPushButton::clicked, this, &PropertyPanel::processRequested);

    // 统一监听所有设置控件变化
    for (QComboBox* cb : findChildren<QComboBox*>())
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PropertyPanel::onSettingsChanged);
    for (QSpinBox* sb : findChildren<QSpinBox*>())
        connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, &PropertyPanel::onSettingsChanged);
    for (QSlider* sl : findChildren<QSlider*>())
        connect(sl, &QSlider::valueChanged, this, &PropertyPanel::onSettingsChanged);
    for (QCheckBox* ch : findChildren<QCheckBox*>())
        connect(ch, &QCheckBox::stateChanged, this, &PropertyPanel::onSettingsChanged);
    for (QLineEdit* le : findChildren<QLineEdit*>())
        connect(le, &QLineEdit::textChanged, this, &PropertyPanel::onSettingsChanged);

    // 输出路径的可编辑文本也需要实时响应
    if (m_stitchOutputDir && m_stitchOutputDir->lineEdit())
        connect(m_stitchOutputDir->lineEdit(), &QLineEdit::textChanged,
                this, &PropertyPanel::onStitchOutputDirChanged);

    loadUiState();
    loadStitchPresets();
    setToolType(ToolType::Stitch);

    QTimer::singleShot(0, this, [this]() { validateAndUpdateUI(); });
}

PropertyPanel::~PropertyPanel()
{
    saveUiState();
}

void PropertyPanel::setToolType(ToolType tool)
{
    m_stack->setCurrentIndex(static_cast<int>(tool));
    validateAndUpdateUI();
}

void PropertyPanel::setImageModel(ImageListModel* model)
{
    if (m_imageModel == model)
        return;
    if (m_imageModel)
        disconnect(m_imageModel, nullptr, this, nullptr);
    m_imageModel = model;
    if (m_imageModel) {
        connect(m_imageModel, &ImageListModel::countChanged,
                this, &PropertyPanel::onImageCountChanged);
        connect(m_imageModel, &ImageListModel::thumbnailLoadFinished,
                this, &PropertyPanel::validateAndUpdateUI);
    }
    refreshStitchPresetAvailability();
    validateAndUpdateUI();
}

QWidget* PropertyPanel::createFormRow(const QString& label, QWidget* widget)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(new QLabel(label, row));
    layout->addWidget(widget, 1);
    return row;
}

QWidget* PropertyPanel::createFormRow(const QString& label, QWidget* widget, QWidget*& rowStorage)
{
    rowStorage = createFormRow(label, widget);
    return rowStorage;
}

void PropertyPanel::buildStitchPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(10);

    // 拼接设置
    m_stitchSectionSettings = new CollapsibleSection(tr("Stitch Settings"), panel);
    QWidget* settingsContent = new QWidget(m_stitchSectionSettings);
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);

    m_stitchDirection = new QComboBox(settingsContent);
    m_stitchDirection->addItem(tr("Vertical"), static_cast<int>(StitchSettings::Vertical));
    m_stitchDirection->addItem(tr("Horizontal"), static_cast<int>(StitchSettings::Horizontal));
    m_stitchDirection->addItem(tr("Grid"), static_cast<int>(StitchSettings::Grid));
    settingsLayout->addWidget(createFormRow(tr("Direction:"), m_stitchDirection));

    m_stitchSpacing = new QSpinBox(settingsContent);
    m_stitchSpacing->setRange(0, 200);
    m_stitchSpacing->setSuffix(QStringLiteral(" px"));
    m_stitchSpacing->setValue(0);
    settingsLayout->addWidget(createFormRow(tr("Spacing:"), m_stitchSpacing));

    m_stitchBackground = new QComboBox(settingsContent);
    m_stitchBackground->addItem(tr("Transparent"), QStringLiteral("transparent"));
    m_stitchBackground->addItem(tr("White"), QStringLiteral("white"));
    m_stitchBackground->addItem(tr("Custom"), QStringLiteral("custom"));
    settingsLayout->addWidget(createFormRow(tr("Background:"), m_stitchBackground));

    QWidget* bgColorRow = new QWidget(settingsContent);
    QHBoxLayout* bgColorLayout = new QHBoxLayout(bgColorRow);
    bgColorLayout->setContentsMargins(0, 2, 0, 2);
    bgColorLayout->setSpacing(6);
    bgColorLayout->addWidget(new QLabel(tr("BG Color:"), bgColorRow));
    m_stitchBgColorBtn = new QPushButton(bgColorRow);
    m_stitchBgColorBtn->setFixedSize(QSize(28, 22));
    m_stitchBgColorBtn->setStyleSheet(QStringLiteral("background-color: white; border: 1px solid gray;"));
    connect(m_stitchBgColorBtn, &QPushButton::clicked, this, &PropertyPanel::onStitchBgColorClicked);
    bgColorLayout->addWidget(m_stitchBgColorBtn);
    m_stitchBgColorLabel = new QLabel(QStringLiteral("#FFFFFF"), bgColorRow);
    bgColorLayout->addWidget(m_stitchBgColorLabel, 1);
    bgColorLayout->addStretch();
    settingsLayout->addWidget(bgColorRow);

    m_stitchUniformWidth = new QCheckBox(tr("Uniform Width"), settingsContent);
    m_stitchRemoveWhiteEdges = new QCheckBox(tr("Remove White Edges"), settingsContent);
    m_stitchAutoCropEdges = new QCheckBox(tr("Auto Crop Edges"), settingsContent);
    settingsLayout->addWidget(m_stitchUniformWidth);
    settingsLayout->addWidget(m_stitchRemoveWhiteEdges);
    settingsLayout->addWidget(m_stitchAutoCropEdges);

    QWidget* gridRow = new QWidget(settingsContent);
    QHBoxLayout* gridLayout = new QHBoxLayout(gridRow);
    gridLayout->setContentsMargins(0, 2, 0, 2);
    gridLayout->setSpacing(6);
    gridLayout->addWidget(new QLabel(tr("Grid:"), gridRow));
    m_stitchGridRows = new QSpinBox(gridRow);
    m_stitchGridRows->setRange(1, 20);
    m_stitchGridRows->setValue(1);
    m_stitchGridColumns = new QSpinBox(gridRow);
    m_stitchGridColumns->setRange(1, 20);
    m_stitchGridColumns->setValue(1);
    gridLayout->addWidget(m_stitchGridRows);
    gridLayout->addWidget(new QLabel(tr("x"), gridRow));
    gridLayout->addWidget(m_stitchGridColumns);
    settingsLayout->addWidget(gridRow);

    m_stitchOutputFormat = new QComboBox(settingsContent);
    m_stitchOutputFormat->addItems(QStringList() << QStringLiteral("PNG") << QStringLiteral("JPG")
                                                  << QStringLiteral("WebP") << QStringLiteral("BMP"));
    settingsLayout->addWidget(createFormRow(tr("Format:"), m_stitchOutputFormat));

    m_stitchQuality = new QSlider(Qt::Horizontal, settingsContent);
    m_stitchQuality->setRange(1, 100);
    m_stitchQuality->setValue(90);
    settingsLayout->addWidget(createFormRow(tr("Quality:"), m_stitchQuality));

    settingsLayout->addStretch();
    m_stitchSectionSettings->setContent(settingsContent);
    panelLayout->addWidget(m_stitchSectionSettings);

    // 拼接预设
    m_stitchSectionPresets = new CollapsibleSection(tr("Grid Presets"), panel);
    QWidget* presetsContent = new QWidget(m_stitchSectionPresets);
    QVBoxLayout* presetsLayout = new QVBoxLayout(presetsContent);
    presetsLayout->setContentsMargins(0, 0, 0, 0);
    presetsLayout->setSpacing(6);

    m_stitchPresetCategory = new QComboBox(presetsContent);
    m_stitchPresetCategory->addItem(tr("All"), QStringLiteral("all"));
    m_stitchPresetCategory->addItem(tr("Social"), QStringLiteral("社交分享"));
    m_stitchPresetCategory->addItem(tr("ID Photo"), QStringLiteral("证件照片"));
    m_stitchPresetCategory->addItem(tr("Custom"), QStringLiteral("自定义"));
    presetsLayout->addWidget(createFormRow(tr("Category:"), m_stitchPresetCategory));

    m_stitchPreset = new QComboBox(presetsContent);
    presetsLayout->addWidget(createFormRow(tr("Preset:"), m_stitchPreset));

    QPushButton* addPresetBtn = new QPushButton(tr("+ Add Preset"), presetsContent);
    connect(addPresetBtn, &QPushButton::clicked, this, &PropertyPanel::onStitchAddPreset);
    presetsLayout->addWidget(addPresetBtn);

    m_stitchSectionPresets->setContent(presetsContent);
    panelLayout->addWidget(m_stitchSectionPresets);

    connect(m_stitchPresetCategory, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onStitchCategoryChanged);
    connect(m_stitchPreset, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onStitchPresetChanged);

    // 输出设置
    m_stitchSectionOutput = new CollapsibleSection(tr("Output Settings"), panel);
    QWidget* outputContent = new QWidget(m_stitchSectionOutput);
    QVBoxLayout* outputLayout = new QVBoxLayout(outputContent);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(8);

    QWidget* dirRow = new QWidget(outputContent);
    QHBoxLayout* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->setSpacing(6);
    dirLayout->addWidget(new QLabel(tr("Output Dir:"), dirRow));
    m_stitchOutputDir = new QComboBox(dirRow);
    m_stitchOutputDir->setEditable(true);
    m_stitchOutputDir->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (m_stitchOutputDir->lineEdit()) {
        connect(m_stitchOutputDir->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            QString path = m_stitchOutputDir->currentText();
            if (!path.isEmpty() && QFileInfo::exists(path) && canWriteToPath(path))
                addOutputHistory(path);
        });
    }
    dirLayout->addWidget(m_stitchOutputDir, 1);
    m_stitchOutputBrowseBtn = new QPushButton(tr("Browse..."), dirRow);
    connect(m_stitchOutputBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                        m_stitchOutputDir->currentText());
        if (!dir.isEmpty()) {
            m_stitchOutputDir->setCurrentText(dir);
            addOutputHistory(dir);
            onStitchOutputDirChanged();
        }
    });
    dirLayout->addWidget(m_stitchOutputBrowseBtn);
    m_stitchOutputCreateBtn = new QPushButton(tr("Create"), dirRow);
    connect(m_stitchOutputCreateBtn, &QPushButton::clicked, this, &PropertyPanel::onStitchCreateOutputDir);
    m_stitchOutputCreateBtn->setVisible(false);
    dirLayout->addWidget(m_stitchOutputCreateBtn);
    outputLayout->addWidget(dirRow);

    m_stitchFileNameTemplate = new QLineEdit(outputContent);
    m_stitchFileNameTemplate->setText(QStringLiteral("{name}_stitched_{time}"));
    m_stitchFileNameTemplate->setPlaceholderText(tr("e.g. {name}_{index}"));
    connect(m_stitchFileNameTemplate, &QLineEdit::textChanged,
            this, &PropertyPanel::onStitchFileNameTemplateChanged);
    outputLayout->addWidget(createFormRow(tr("File Name:"), m_stitchFileNameTemplate));

    m_stitchFileNamePreview = new QLabel(outputContent);
    m_stitchFileNamePreview->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    m_stitchFileNamePreview->setWordWrap(true);
    outputLayout->addWidget(m_stitchFileNamePreview);

    m_stitchValidationLabel = new QLabel(outputContent);
    m_stitchValidationLabel->setWordWrap(true);
    m_stitchValidationLabel->setMinimumHeight(32);
    outputLayout->addWidget(m_stitchValidationLabel);

    m_stitchSectionOutput->setContent(outputContent);
    panelLayout->addWidget(m_stitchSectionOutput);

    panelLayout->addStretch();
    m_stack->addWidget(panel);

    connect(m_stitchOutputFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onStitchOutputFormatChanged);
    onStitchFileNameTemplateChanged();
}

void PropertyPanel::buildConvertPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>Convert Settings</b>"), panel));

    m_convertFormat = new QComboBox(panel);
    m_convertFormat->addItems(QStringList() << QStringLiteral("PNG") << QStringLiteral("JPG")
                                            << QStringLiteral("WebP") << QStringLiteral("GIF")
                                            << QStringLiteral("BMP") << QStringLiteral("TIFF"));
    layout->addWidget(createFormRow(tr("Target Format:"), m_convertFormat));

    m_convertQuality = new QSlider(Qt::Horizontal, panel);
    m_convertQuality->setRange(1, 100);
    m_convertQuality->setValue(90);
    layout->addWidget(createFormRow(tr("Quality:"), m_convertQuality));

    m_convertKeepExif = new QCheckBox(tr("Keep EXIF"), panel);
    m_convertKeepExif->setChecked(true);
    layout->addWidget(m_convertKeepExif);

    m_convertToSRgb = new QCheckBox(tr("Convert to sRGB"), panel);
    layout->addWidget(m_convertToSRgb);

    m_convertEstimateLabel = new QLabel(panel);
    m_convertEstimateLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    layout->addWidget(m_convertEstimateLabel);

    m_convertEstimateTimer = new QTimer(this);
    m_convertEstimateTimer->setSingleShot(true);
    m_convertEstimateTimer->setInterval(200);
    connect(m_convertEstimateTimer, &QTimer::timeout, this, &PropertyPanel::onConvertEstimateTimeout);

    connect(m_convertFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onConvertFormatChanged);
    connect(m_convertQuality, &QSlider::valueChanged, this, &PropertyPanel::onConvertFormatChanged);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildCompressPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>Compress Settings</b>"), panel));

    m_compressMode = new QComboBox(panel);
    m_compressMode->addItem(tr("Quality"), static_cast<int>(CompressMode::Quality));
    m_compressMode->addItem(tr("Size"), static_cast<int>(CompressMode::Size));
    m_compressMode->addItem(tr("Smart"), static_cast<int>(CompressMode::Smart));
    layout->addWidget(createFormRow(tr("Mode:"), m_compressMode));

    m_compressStrength = new QSlider(Qt::Horizontal, panel);
    m_compressStrength->setRange(0, 100);
    m_compressStrength->setValue(50);
    layout->addWidget(createFormRow(tr("Strength:"), m_compressStrength));

    m_compressQuality = new QSlider(Qt::Horizontal, panel);
    m_compressQuality->setRange(1, 100);
    m_compressQuality->setValue(80);
    layout->addWidget(createFormRow(tr("Quality:"), m_compressQuality));

    m_compressScale = new QSpinBox(panel);
    m_compressScale->setRange(10, 100);
    m_compressScale->setSuffix(QStringLiteral("%"));
    m_compressScale->setValue(100);
    layout->addWidget(createFormRow(tr("Scale:"), m_compressScale));

    QWidget* targetSizeRow = new QWidget(panel);
    QHBoxLayout* targetSizeLayout = new QHBoxLayout(targetSizeRow);
    targetSizeLayout->setContentsMargins(0, 2, 0, 2);
    targetSizeLayout->setSpacing(6);
    targetSizeLayout->addWidget(new QLabel(tr("Target Size:"), targetSizeRow));
    m_compressTargetSize = new QDoubleSpinBox(targetSizeRow);
    m_compressTargetSize->setRange(0.01, 1000.0);
    m_compressTargetSize->setValue(2.0);
    m_compressTargetSize->setDecimals(2);
    m_compressTargetSize->setSuffix(QStringLiteral(" MB"));
    targetSizeLayout->addWidget(m_compressTargetSize);
    m_compressTargetUnit = new QComboBox(targetSizeRow);
    m_compressTargetUnit->addItem(tr("MB"), QStringLiteral("MB"));
    m_compressTargetUnit->addItem(tr("KB"), QStringLiteral("KB"));
    targetSizeLayout->addWidget(m_compressTargetUnit);
    layout->addWidget(targetSizeRow);

    m_compressShowOriginal = new QCheckBox(tr("Show Original"), panel);
    layout->addWidget(m_compressShowOriginal);

    m_compressEstimateLabel = new QLabel(panel);
    m_compressEstimateLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    layout->addWidget(m_compressEstimateLabel);

    connect(m_compressMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onCompressModeChanged);
    connect(m_compressShowOriginal, &QCheckBox::stateChanged, this, &PropertyPanel::onSettingsChanged);
    onCompressModeChanged(0);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildWatermarkPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>Watermark Settings</b>"), panel));

    m_watermarkType = new QComboBox(panel);
    m_watermarkType->addItem(tr("Text"), static_cast<int>(WatermarkType::Text));
    m_watermarkType->addItem(tr("Image"), static_cast<int>(WatermarkType::Image));
    layout->addWidget(createFormRow(tr("Type:"), m_watermarkType));
    connect(m_watermarkType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onWatermarkTypeChanged);

    m_watermarkText = new QLineEdit(panel);
    m_watermarkText->setText(QStringLiteral("影图 ImagePro"));
    layout->addWidget(createFormRow(tr("Text:"), m_watermarkText));

    QWidget* wmImageRow = new QWidget(panel);
    QHBoxLayout* wmImageLayout = new QHBoxLayout(wmImageRow);
    wmImageLayout->setContentsMargins(0, 2, 0, 2);
    wmImageLayout->setSpacing(6);
    wmImageLayout->addWidget(new QLabel(tr("Image:"), wmImageRow));
    m_watermarkImagePath = new QLineEdit(wmImageRow);
    wmImageLayout->addWidget(m_watermarkImagePath, 1);
    QPushButton* wmBrowseBtn = new QPushButton(tr("Browse..."), wmImageRow);
    connect(wmBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Select Watermark Image"),
                                                    m_watermarkImagePath->text(),
                                                    FileUtils::imageFileFilter());
        if (!path.isEmpty())
            m_watermarkImagePath->setText(path);
    });
    wmImageLayout->addWidget(wmBrowseBtn);
    layout->addWidget(wmImageRow);

    m_watermarkFontFamily = new QComboBox(panel);
    for (const QString& family : QFontDatabase().families()) {
        if (!family.startsWith(QStringLiteral(".")) && !family.isEmpty())
            m_watermarkFontFamily->addItem(family);
    }
    int defaultFontIdx = m_watermarkFontFamily->findText(QStringLiteral("Microsoft YaHei"));
    if (defaultFontIdx < 0)
        defaultFontIdx = m_watermarkFontFamily->findText(QFont().defaultFamily());
    m_watermarkFontFamily->setCurrentIndex(qMax(0, defaultFontIdx));
    layout->addWidget(createFormRow(tr("Font Family:"), m_watermarkFontFamily));

    m_watermarkFontSize = new QSpinBox(panel);
    m_watermarkFontSize->setRange(8, 200);
    m_watermarkFontSize->setValue(24);
    layout->addWidget(createFormRow(tr("Font Size:"), m_watermarkFontSize));

    QWidget* colorRow = new QWidget(panel);
    QHBoxLayout* colorLayout = new QHBoxLayout(colorRow);
    colorLayout->setContentsMargins(0, 2, 0, 2);
    colorLayout->setSpacing(6);
    colorLayout->addWidget(new QLabel(tr("Color:"), colorRow));
    m_watermarkColorBtn = new QPushButton(colorRow);
    m_watermarkColorBtn->setFixedSize(QSize(28, 22));
    m_watermarkColorBtn->setStyleSheet(QStringLiteral("background-color: white; border: 1px solid gray;"));
    connect(m_watermarkColorBtn, &QPushButton::clicked, this, &PropertyPanel::onWatermarkColorClicked);
    colorLayout->addWidget(m_watermarkColorBtn);
    colorLayout->addStretch();
    layout->addWidget(colorRow);

    m_watermarkOpacity = new QSlider(Qt::Horizontal, panel);
    m_watermarkOpacity->setRange(0, 100);
    m_watermarkOpacity->setValue(50);
    layout->addWidget(createFormRow(tr("Opacity:"), m_watermarkOpacity));

    m_watermarkRotation = new QSpinBox(panel);
    m_watermarkRotation->setRange(0, 360);
    m_watermarkRotation->setSuffix(QStringLiteral("°"));
    layout->addWidget(createFormRow(tr("Rotation:"), m_watermarkRotation));

    m_watermarkPosition = new QComboBox(panel);
    m_watermarkPosition->addItem(tr("Top Left"));
    m_watermarkPosition->addItem(tr("Top Center"));
    m_watermarkPosition->addItem(tr("Top Right"));
    m_watermarkPosition->addItem(tr("Center Left"));
    m_watermarkPosition->addItem(tr("Center"));
    m_watermarkPosition->addItem(tr("Center Right"));
    m_watermarkPosition->addItem(tr("Bottom Left"));
    m_watermarkPosition->addItem(tr("Bottom Center"));
    m_watermarkPosition->addItem(tr("Bottom Right"));
    m_watermarkPosition->setCurrentIndex(4);
    layout->addWidget(createFormRow(tr("Position:"), m_watermarkPosition));

    m_watermarkTile = new QCheckBox(tr("Tile"), panel);
    layout->addWidget(m_watermarkTile);

    m_watermarkTileSpacing = new QSpinBox(panel);
    m_watermarkTileSpacing->setRange(0, 500);
    m_watermarkTileSpacing->setSuffix(QStringLiteral(" px"));
    m_watermarkTileSpacing->setValue(100);
    layout->addWidget(createFormRow(tr("Tile Spacing:"), m_watermarkTileSpacing));

    m_watermarkMargin = new QSpinBox(panel);
    m_watermarkMargin->setRange(0, 500);
    m_watermarkMargin->setSuffix(QStringLiteral(" px"));
    m_watermarkMargin->setValue(20);
    layout->addWidget(createFormRow(tr("Margin:"), m_watermarkMargin));

    m_watermarkOutputFormat = new QComboBox(panel);
    m_watermarkOutputFormat->addItem(tr("Original"), QStringLiteral("original"));
    m_watermarkOutputFormat->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_watermarkOutputFormat->addItem(QStringLiteral("JPG"), QStringLiteral("jpg"));
    m_watermarkOutputFormat->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    layout->addWidget(createFormRow(tr("Output Format:"), m_watermarkOutputFormat));

    m_watermarkQuality = new QSlider(Qt::Horizontal, panel);
    m_watermarkQuality->setRange(1, 100);
    m_watermarkQuality->setValue(90);
    layout->addWidget(createFormRow(tr("Quality:"), m_watermarkQuality));

    QWidget* wmOutDirRow = new QWidget(panel);
    QHBoxLayout* wmOutDirLayout = new QHBoxLayout(wmOutDirRow);
    wmOutDirLayout->setContentsMargins(0, 2, 0, 2);
    wmOutDirLayout->setSpacing(6);
    wmOutDirLayout->addWidget(new QLabel(tr("Output Dir:"), wmOutDirRow));
    m_watermarkOutputDir = new QLineEdit(wmOutDirRow);
    wmOutDirLayout->addWidget(m_watermarkOutputDir, 1);
    QPushButton* wmOutBrowseBtn = new QPushButton(tr("Browse..."), wmOutDirRow);
    connect(wmOutBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                        m_watermarkOutputDir->text());
        if (!dir.isEmpty())
            m_watermarkOutputDir->setText(dir);
    });
    wmOutDirLayout->addWidget(wmOutBrowseBtn);
    layout->addWidget(wmOutDirRow);

    onWatermarkTypeChanged(0);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildEditPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>Edit Tools</b>"), panel));

    m_editTool = new QComboBox(panel);
    m_editTool->addItem(tr("Rectangle"), static_cast<int>(EditToolType::Rectangle));
    m_editTool->addItem(tr("Ellipse"), static_cast<int>(EditToolType::Ellipse));
    m_editTool->addItem(tr("Arrow"), static_cast<int>(EditToolType::Arrow));
    m_editTool->addItem(tr("Pen"), static_cast<int>(EditToolType::Pen));
    m_editTool->addItem(tr("Mosaic"), static_cast<int>(EditToolType::Mosaic));
    m_editTool->addItem(tr("Text"), static_cast<int>(EditToolType::Text));
    m_editTool->addItem(tr("Crop"), static_cast<int>(EditToolType::Crop));
    m_editTool->addItem(tr("Filter"), static_cast<int>(EditToolType::Filter));
    layout->addWidget(createFormRow(tr("Tool:"), m_editTool));

    m_editFilterType = new QComboBox(panel);
    m_editFilterType->addItem(tr("Grayscale"), static_cast<int>(FilterType::Grayscale));
    m_editFilterType->addItem(tr("Sepia"), static_cast<int>(FilterType::Sepia));
    m_editFilterType->addItem(tr("Warm"), static_cast<int>(FilterType::Warm));
    m_editFilterType->addItem(tr("Cool"), static_cast<int>(FilterType::Cool));
    m_editFilterType->addItem(tr("High Contrast"), static_cast<int>(FilterType::HighContrast));
    m_editFilterType->addItem(tr("Blur"), static_cast<int>(FilterType::Blur));
    m_editFilterType->addItem(tr("Sharpen"), static_cast<int>(FilterType::Sharpen));
    layout->addWidget(createFormRow(tr("Filter:"), m_editFilterType));

    m_editColor = new QComboBox(panel);
    m_editColor->addItem(tr("Red"), QColor(Qt::red));
    m_editColor->addItem(tr("Blue"), QColor(Qt::blue));
    m_editColor->addItem(tr("Green"), QColor(Qt::green));
    m_editColor->addItem(tr("Yellow"), QColor(Qt::yellow));
    m_editColor->addItem(tr("Black"), QColor(Qt::black));
    m_editColor->addItem(tr("White"), QColor(Qt::white));
    m_editColor->setCurrentIndex(0);
    layout->addWidget(createFormRow(tr("Color:"), m_editColor));

    m_editLineWidth = new QSpinBox(panel);
    m_editLineWidth->setRange(1, 20);
    m_editLineWidth->setValue(3);
    layout->addWidget(createFormRow(tr("Line Width:"), m_editLineWidth));

    m_editOpacity = new QSlider(Qt::Horizontal, panel);
    m_editOpacity->setRange(0, 100);
    m_editOpacity->setValue(80);
    layout->addWidget(createFormRow(tr("Opacity:"), m_editOpacity));

    m_editFillStyle = new QComboBox(panel);
    m_editFillStyle->addItem(tr("No Fill"), static_cast<int>(EditFillStyle::NoFill));
    m_editFillStyle->addItem(tr("Semi Fill"), static_cast<int>(EditFillStyle::SemiFill));
    m_editFillStyle->addItem(tr("Solid Fill"), static_cast<int>(EditFillStyle::SolidFill));
    m_editFillStyle->setCurrentIndex(1);
    layout->addWidget(createFormRow(tr("Fill Style:"), m_editFillStyle));

    m_editFontSize = new QSpinBox(panel);
    m_editFontSize->setRange(8, 200);
    m_editFontSize->setValue(16);
    layout->addWidget(createFormRow(tr("Font Size:"), m_editFontSize));

    QHBoxLayout* historyBtnLayout = new QHBoxLayout();
    historyBtnLayout->setSpacing(6);
    m_editUndoBtn = new QPushButton(tr("Undo"), panel);
    m_editRedoBtn = new QPushButton(tr("Redo"), panel);
    m_editClearBtn = new QPushButton(tr("Clear"), panel);
    historyBtnLayout->addWidget(m_editUndoBtn);
    historyBtnLayout->addWidget(m_editRedoBtn);
    historyBtnLayout->addWidget(m_editClearBtn);
    layout->addLayout(historyBtnLayout);

    m_editHistoryList = new QListWidget(panel);
    m_editHistoryList->setMaximumHeight(160);
    layout->addWidget(m_editHistoryList);

    connect(m_editUndoBtn, &QPushButton::clicked, this, &PropertyPanel::onEditUndo);
    connect(m_editRedoBtn, &QPushButton::clicked, this, &PropertyPanel::onEditRedo);
    connect(m_editClearBtn, &QPushButton::clicked, this, &PropertyPanel::onEditClear);
    connect(m_editHistoryList, &QListWidget::itemClicked, this, &PropertyPanel::onEditHistoryItemClicked);
    connect(m_editTool, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onEditToolChanged);

    onEditToolChanged(0);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildResizePanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>Resize Settings</b>"), panel));

    m_resizeMode = new QComboBox(panel);
    m_resizeMode->addItem(tr("Percentage"), static_cast<int>(ResizeMode::Percentage));
    m_resizeMode->addItem(tr("Pixel"), static_cast<int>(ResizeMode::Pixel));
    m_resizeMode->addItem(tr("Preset"), static_cast<int>(ResizeMode::Preset));
    layout->addWidget(createFormRow(tr("Mode:"), m_resizeMode));

    m_resizePercentage = new QSpinBox(panel);
    m_resizePercentage->setRange(10, 500);
    m_resizePercentage->setSuffix(QStringLiteral("%"));
    m_resizePercentage->setValue(100);
    layout->addWidget(createFormRow(tr("Percentage:"), m_resizePercentage));

    m_resizeWidth = new QSpinBox(panel);
    m_resizeWidth->setRange(1, 10000);
    m_resizeWidth->setValue(800);
    m_resizeHeight = new QSpinBox(panel);
    m_resizeHeight->setRange(1, 10000);
    m_resizeHeight->setValue(600);

    m_resizeLockAspect = new QToolButton(panel);
    m_resizeLockAspect->setCheckable(true);
    m_resizeLockAspect->setChecked(true);
    m_resizeLockAspect->setText(QStringLiteral("🔒"));
    m_resizeLockAspect->setToolTip(tr("Lock aspect ratio"));

    QHBoxLayout* sizeLayout = new QHBoxLayout();
    sizeLayout->setSpacing(6);
    sizeLayout->addWidget(new QLabel(tr("Size:"), panel));
    sizeLayout->addWidget(m_resizeWidth);
    sizeLayout->addWidget(new QLabel(tr("x"), panel));
    sizeLayout->addWidget(m_resizeHeight);
    sizeLayout->addWidget(m_resizeLockAspect);
    layout->addLayout(sizeLayout);

    m_resizeAspectLabel = new QLabel(panel);
    m_resizeAspectLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    layout->addWidget(m_resizeAspectLabel);

    m_resizeInterpolation = new QComboBox(panel);
    m_resizeInterpolation->addItem(tr("Nearest"), static_cast<int>(Interpolation::Nearest));
    m_resizeInterpolation->addItem(tr("Bilinear"), static_cast<int>(Interpolation::Bilinear));
    m_resizeInterpolation->addItem(tr("Bicubic"), static_cast<int>(Interpolation::Bicubic));
    m_resizeInterpolation->addItem(tr("Lanczos"), static_cast<int>(Interpolation::Lanczos));
    m_resizeInterpolation->setCurrentIndex(1);
    layout->addWidget(createFormRow(tr("Interpolation:"), m_resizeInterpolation));

    m_resizeFitWithinOriginal = new QCheckBox(tr("Do not exceed original size"), panel);
    layout->addWidget(m_resizeFitWithinOriginal);

    m_resizeCategory = new QComboBox(panel);
    m_resizeCategory->setEnabled(false);
    layout->addWidget(createFormRow(tr("Category:"), m_resizeCategory));

    m_resizePreset = new QComboBox(panel);
    m_resizePreset->setEnabled(false);
    layout->addWidget(createFormRow(tr("Preset:"), m_resizePreset));

    QHBoxLayout* presetBtnLayout = new QHBoxLayout();
    presetBtnLayout->setSpacing(6);
    m_resizeAddPresetBtn = new QPushButton(tr("Add"), panel);
    m_resizeDeletePresetBtn = new QPushButton(tr("Delete"), panel);
    m_resizeAddPresetBtn->setEnabled(false);
    m_resizeDeletePresetBtn->setEnabled(false);
    presetBtnLayout->addWidget(m_resizeAddPresetBtn);
    presetBtnLayout->addWidget(m_resizeDeletePresetBtn);
    layout->addLayout(presetBtnLayout);

    m_resizeOutputFormat = new QComboBox(panel);
    m_resizeOutputFormat->addItem(tr("Original"), QStringLiteral("original"));
    m_resizeOutputFormat->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_resizeOutputFormat->addItem(QStringLiteral("JPG"), QStringLiteral("jpg"));
    m_resizeOutputFormat->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    layout->addWidget(createFormRow(tr("Output Format:"), m_resizeOutputFormat));

    m_resizeQuality = new QSlider(Qt::Horizontal, panel);
    m_resizeQuality->setRange(1, 100);
    m_resizeQuality->setValue(90);
    layout->addWidget(createFormRow(tr("Quality:"), m_resizeQuality));

    QWidget* resizeOutDirRow = new QWidget(panel);
    QHBoxLayout* resizeOutDirLayout = new QHBoxLayout(resizeOutDirRow);
    resizeOutDirLayout->setContentsMargins(0, 2, 0, 2);
    resizeOutDirLayout->setSpacing(6);
    resizeOutDirLayout->addWidget(new QLabel(tr("Output Dir:"), resizeOutDirRow));
    m_resizeOutputDir = new QLineEdit(resizeOutDirRow);
    resizeOutDirLayout->addWidget(m_resizeOutputDir, 1);
    QPushButton* resizeOutBrowseBtn = new QPushButton(tr("Browse..."), resizeOutDirRow);
    connect(resizeOutBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                        m_resizeOutputDir->text());
        if (!dir.isEmpty())
            m_resizeOutputDir->setText(dir);
    });
    resizeOutDirLayout->addWidget(resizeOutBrowseBtn);
    layout->addWidget(resizeOutDirRow);

    m_resizeDelayTimer = new QTimer(this);
    m_resizeDelayTimer->setSingleShot(true);
    m_resizeDelayTimer->setInterval(200);
    connect(m_resizeDelayTimer, &QTimer::timeout, this, &PropertyPanel::previewRequested);

    connect(m_resizeMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onResizeModeChanged);
    connect(m_resizeCategory, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onResizeCategoryChanged);
    connect(m_resizePreset, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::onResizePresetChanged);
    connect(m_resizeWidth, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PropertyPanel::onResizeWidthChanged);
    connect(m_resizeHeight, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PropertyPanel::onResizeHeightChanged);
    connect(m_resizeLockAspect, &QToolButton::toggled,
            this, &PropertyPanel::onSettingsChanged);
    connect(m_resizeAddPresetBtn, &QPushButton::clicked, this, &PropertyPanel::onResizeAddPreset);
    connect(m_resizeDeletePresetBtn, &QPushButton::clicked, this, &PropertyPanel::onResizeDeletePreset);

    loadResizePresetCategories();
    onResizeCategoryChanged(0);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildBatchPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>Batch Processing</b>"), panel));

    m_batchTargetTool = new QComboBox(panel);
    m_batchTargetTool->addItem(tr("Convert"), static_cast<int>(ToolType::Convert));
    m_batchTargetTool->addItem(tr("Compress"), static_cast<int>(ToolType::Compress));
    m_batchTargetTool->addItem(tr("Watermark"), static_cast<int>(ToolType::Watermark));
    m_batchTargetTool->addItem(tr("Resize"), static_cast<int>(ToolType::Resize));
    layout->addWidget(createFormRow(tr("Target Tool:"), m_batchTargetTool));

    m_batchOutputDir = new QLineEdit(panel);
    QPushButton* browseBtn = new QPushButton(tr("Browse..."), panel);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                        m_batchOutputDir->text());
        if (!dir.isEmpty())
            m_batchOutputDir->setText(dir);
    });

    QHBoxLayout* dirLayout = new QHBoxLayout();
    dirLayout->setSpacing(6);
    dirLayout->addWidget(new QLabel(tr("Output Dir:"), panel));
    dirLayout->addWidget(m_batchOutputDir, 1);
    dirLayout->addWidget(browseBtn);
    layout->addLayout(dirLayout);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildPdfPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>PDF Export</b>"), panel));

    m_pdfPageSize = new QComboBox(panel);
    m_pdfPageSize->addItem(tr("A4"), static_cast<int>(PdfSettings::A4));
    m_pdfPageSize->addItem(tr("A5"), static_cast<int>(PdfSettings::A5));
    m_pdfPageSize->addItem(tr("Letter"), static_cast<int>(PdfSettings::Letter));
    layout->addWidget(createFormRow(tr("Page Size:"), m_pdfPageSize));

    m_pdfLayout = new QComboBox(panel);
    m_pdfLayout->addItem(tr("Single Per Page"), static_cast<int>(PdfSettings::SinglePerPage));
    m_pdfLayout->addItem(tr("Fit to Page"), static_cast<int>(PdfSettings::FitToPage));
    m_pdfLayout->addItem(tr("Grid 2x2"), static_cast<int>(PdfSettings::Grid2x2));
    m_pdfLayout->addItem(tr("Grid 3x3"), static_cast<int>(PdfSettings::Grid3x3));
    layout->addWidget(createFormRow(tr("Layout:"), m_pdfLayout));

    m_pdfDpi = new QSpinBox(panel);
    m_pdfDpi->setRange(72, 600);
    m_pdfDpi->setValue(150);
    m_pdfDpi->setSuffix(QStringLiteral(" dpi"));
    layout->addWidget(createFormRow(tr("Resolution:"), m_pdfDpi));

    QWidget* marginRow = new QWidget(panel);
    QHBoxLayout* marginLayout = new QHBoxLayout(marginRow);
    marginLayout->setContentsMargins(0, 2, 0, 2);
    marginLayout->setSpacing(6);
    marginLayout->addWidget(new QLabel(tr("Margins:"), marginRow));
    m_pdfMarginLeft = new QDoubleSpinBox(marginRow);
    m_pdfMarginTop = new QDoubleSpinBox(marginRow);
    m_pdfMarginRight = new QDoubleSpinBox(marginRow);
    m_pdfMarginBottom = new QDoubleSpinBox(marginRow);
    for (auto* sb : { m_pdfMarginLeft, m_pdfMarginTop, m_pdfMarginRight, m_pdfMarginBottom }) {
        sb->setRange(0, 100);
        sb->setValue(20.0);
        sb->setDecimals(1);
        sb->setSuffix(QStringLiteral(" mm"));
        marginLayout->addWidget(sb);
    }
    layout->addWidget(marginRow);

    QWidget* outRow = new QWidget(panel);
    QHBoxLayout* outLayout = new QHBoxLayout(outRow);
    outLayout->setContentsMargins(0, 2, 0, 2);
    outLayout->setSpacing(6);
    outLayout->addWidget(new QLabel(tr("Output:"), outRow));
    m_pdfOutputPath = new QLineEdit(outRow);
    outLayout->addWidget(m_pdfOutputPath, 1);
    QPushButton* browseBtn = new QPushButton(tr("Browse..."), outRow);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Save PDF"),
                                                    m_pdfOutputPath->text(),
                                                    tr("PDF Files (*.pdf)"));
        if (!path.isEmpty())
            m_pdfOutputPath->setText(path);
    });
    outLayout->addWidget(browseBtn);
    layout->addWidget(outRow);

    layout->addStretch();
    m_stack->addWidget(panel);
}

StitchSettings PropertyPanel::stitchSettings() const
{
    StitchSettings s;
    s.direction = static_cast<StitchSettings::Direction>(m_stitchDirection->currentData().toInt());
    s.spacing = m_stitchSpacing->value();
    s.background = m_stitchBackground->currentData().toString();
    s.bgColor = QColor(m_stitchBgColorLabel->text());
    s.uniformWidth = m_stitchUniformWidth->isChecked();
    s.removeWhiteEdges = m_stitchRemoveWhiteEdges->isChecked();
    s.autoCropEdges = m_stitchAutoCropEdges->isChecked();
    s.gridRows = m_stitchGridRows->value();
    s.gridColumns = m_stitchGridColumns->value();
    s.outputFormat = m_stitchOutputFormat->currentText().toLower();
    s.quality = m_stitchQuality->value();
    s.outputDir = m_stitchOutputDir ? m_stitchOutputDir->currentText() : QString();
    return s;
}

ConvertSettings PropertyPanel::convertSettings() const
{
    ConvertSettings s;
    s.targetFormat = m_convertFormat->currentText().toLower();
    s.quality = m_convertQuality->value();
    s.keepExif = m_convertKeepExif->isChecked();
    s.convertToSRgb = m_convertToSRgb->isChecked();
    return s;
}

CompressSettings PropertyPanel::compressSettings() const
{
    CompressSettings s;
    s.mode = static_cast<CompressMode>(m_compressMode->currentData().toInt());
    s.strength = m_compressStrength->value();
    s.quality = m_compressQuality->value();
    s.scalePercent = m_compressScale->value();

    double value = m_compressTargetSize->value();
    QString unit = m_compressTargetUnit->currentData().toString();
    if (unit == QStringLiteral("KB"))
        s.targetSize = qRound64(value * 1024);
    else
        s.targetSize = qRound64(value * 1024 * 1024);
    s.showOriginal = m_compressShowOriginal->isChecked();
    return s;
}

WatermarkSettings PropertyPanel::watermarkSettings() const
{
    WatermarkSettings s;
    s.type = static_cast<WatermarkType>(m_watermarkType->currentData().toInt());
    s.text = m_watermarkText->text();
    s.imagePath = m_watermarkImagePath->text();
    s.fontFamily = m_watermarkFontFamily->currentText();
    s.fontSize = m_watermarkFontSize->value();
    s.color = m_watermarkColor;
    s.opacity = m_watermarkOpacity->value();
    s.rotation = m_watermarkRotation->value();
    s.position = m_watermarkPosition->currentIndex();
    s.tile = m_watermarkTile->isChecked();
    s.tileSpacing = m_watermarkTileSpacing->value();
    s.margin = m_watermarkMargin->value();
    s.outputFormat = m_watermarkOutputFormat->currentData().toString();
    s.quality = m_watermarkQuality->value();
    s.outputDir = m_watermarkOutputDir->text();
    return s;
}

EditAction PropertyPanel::currentEditAction() const
{
    EditAction a;
    a.toolType = static_cast<EditToolType>(m_editTool->currentData().toInt());
    a.filterType = static_cast<FilterType>(m_editFilterType->currentData().toInt());
    a.color = m_editColor->currentData().value<QColor>();
    a.lineWidth = m_editLineWidth->value();
    a.opacity = m_editOpacity->value();
    a.fontSize = m_editFontSize->value();
    a.fillStyle = static_cast<EditFillStyle>(m_editFillStyle->currentData().toInt());
    return a;
}

PdfSettings PropertyPanel::pdfSettings() const
{
    PdfSettings s;
    s.pageSize = static_cast<PdfSettings::PageSize>(m_pdfPageSize->currentData().toInt());
    s.layout = static_cast<PdfSettings::Layout>(m_pdfLayout->currentData().toInt());
    s.dpi = m_pdfDpi->value();
    s.marginLeft = m_pdfMarginLeft->value();
    s.marginTop = m_pdfMarginTop->value();
    s.marginRight = m_pdfMarginRight->value();
    s.marginBottom = m_pdfMarginBottom->value();
    s.outputPath = m_pdfOutputPath->text();
    return s;
}

ResizeSettings PropertyPanel::resizeSettings() const
{
    ResizeSettings s;
    s.mode = static_cast<ResizeMode>(m_resizeMode->currentData().toInt());
    s.percentage = m_resizePercentage->value();
    s.targetWidth = m_resizeWidth->value();
    s.targetHeight = m_resizeHeight->value();
    s.lockAspectRatio = m_resizeLockAspect->isChecked();
    s.fitWithinOriginal = m_resizeFitWithinOriginal->isChecked();
    s.interpolation = static_cast<Interpolation>(m_resizeInterpolation->currentData().toInt());
    s.outputFormat = m_resizeOutputFormat->currentData().toString();
    s.quality = m_resizeQuality->value();
    s.outputDir = m_resizeOutputDir->text();

    if (s.mode == ResizeMode::Preset && m_resizePreset->currentIndex() >= 0) {
        QString presetId = m_resizePreset->currentData().toString();
        for (const auto& p : m_resizePresets) {
            if (p.id == presetId) {
                s.targetWidth = p.width;
                s.targetHeight = p.height;
                break;
            }
        }
    }
    return s;
}

QString PropertyPanel::outputFileNameTemplate() const
{
    return m_stitchFileNameTemplate ? m_stitchFileNameTemplate->text() : QString();
}

PropertyPanel::BatchSettings PropertyPanel::batchSettings() const
{
    BatchSettings settings;
    settings.targetTool = static_cast<ToolType>(m_batchTargetTool->currentData().toInt());
    settings.outputDir = m_batchOutputDir->text();
    return settings;
}

void PropertyPanel::updatePreview(const QImage& image)
{
    m_lastImage = image;
}

void PropertyPanel::onSettingsChanged()
{
    if (!m_applyingStitchPreset && m_stack->currentIndex() == static_cast<int>(ToolType::Stitch)) {
        if (!m_currentStitchPresetId.isEmpty())
            clearStitchPresetSelection();
    }

    emit settingsChanged();
    validateAndUpdateUI();
}

void PropertyPanel::hideEvent(QHideEvent* event)
{
    saveUiState();
    QWidget::hideEvent(event);
}

void PropertyPanel::loadStitchPresets()
{
    m_stitchPresets = StitchPresetManager::instance().loadPresets();
    rebuildStitchPresetDropdown();
}

QString PropertyPanel::stitchPresetDisplayText(const StitchPreset& preset) const
{
    return QStringLiteral("%1 (%2x%3)").arg(preset.name).arg(preset.rows).arg(preset.columns);
}

void PropertyPanel::rebuildStitchPresetDropdown()
{
    if (!m_stitchPreset || !m_stitchPresetCategory)
        return;

    m_rebuildingStitchPresets = true;
    QString selectedId = m_currentStitchPresetId;

    m_stitchPreset->blockSignals(true);
    m_stitchPreset->clear();
    m_stitchPreset->addItem(tr("Custom"), QString());

    QString catData = m_stitchPresetCategory->currentData().toString();
    int imageCount = m_imageModel ? m_imageModel->validCount() : 9999;

    int selectIndex = 0;
    for (const auto& p : m_stitchPresets) {
        bool show = (catData == QStringLiteral("all")) || (p.category == catData);
        if (!show)
            continue;

        int required = p.rows * p.columns;
        bool available = imageCount >= required;
        QString text = stitchPresetDisplayText(p);
        if (!available)
            text += tr(" [needs %1]").arg(required);

        int idx = m_stitchPreset->count();
        m_stitchPreset->addItem(text, p.id);
        if (p.id == selectedId)
            selectIndex = idx;

        // 禁用不可用的内置预设项
        if (!available) {
            QStandardItemModel* model = qobject_cast<QStandardItemModel*>(m_stitchPreset->model());
            if (model) {
                QStandardItem* item = model->item(idx);
                if (item)
                    item->setEnabled(false);
            }
        }
    }

    m_stitchPreset->setCurrentIndex(selectIndex);
    m_stitchPreset->blockSignals(false);
    m_rebuildingStitchPresets = false;
}

void PropertyPanel::onStitchCategoryChanged(int /*index*/)
{
    rebuildStitchPresetDropdown();
}

void PropertyPanel::onStitchPresetChanged(int index)
{
    if (m_rebuildingStitchPresets || m_applyingStitchPreset)
        return;

    if (index <= 0) {
        if (!m_currentStitchPresetId.isEmpty()) {
            m_currentStitchPresetId.clear();
            emit settingsChanged();
        }
        return;
    }

    QString presetId = m_stitchPreset->itemData(index).toString();
    if (m_currentStitchPresetId == presetId)
        return;

    m_currentStitchPresetId = presetId;

    if (m_stitchSectionSettings)
        m_stitchSectionSettings->setExpanded(true);

    for (const auto& p : m_stitchPresets) {
        if (p.id == presetId) {
            applyStitchPreset(p);
            break;
        }
    }

    emit settingsChanged();
}

void PropertyPanel::selectStitchPresetById(const QString& presetId)
{
    m_currentStitchPresetId = presetId;
    if (!m_stitchPreset)
        return;
    for (int i = 0; i < m_stitchPreset->count(); ++i) {
        if (m_stitchPreset->itemData(i).toString() == presetId) {
            m_stitchPreset->setCurrentIndex(i);
            return;
        }
    }
    m_stitchPreset->setCurrentIndex(0);
}

void PropertyPanel::clearStitchPresetSelection()
{
    m_currentStitchPresetId.clear();
    if (m_stitchPreset)
        m_stitchPreset->setCurrentIndex(0);
}

void PropertyPanel::refreshStitchPresetAvailability()
{
    rebuildStitchPresetDropdown();
}

void PropertyPanel::applyStitchPreset(const StitchPreset& preset)
{
    if (preset.isBuiltIn && preset.name == QStringLiteral("Custom"))
        return;

    m_applyingStitchPreset = true;
    m_stitchDirection->blockSignals(true);
    m_stitchGridRows->blockSignals(true);
    m_stitchGridColumns->blockSignals(true);
    m_stitchSpacing->blockSignals(true);
    m_stitchUniformWidth->blockSignals(true);

    m_stitchDirection->setCurrentIndex(m_stitchDirection->findData(static_cast<int>(StitchSettings::Grid)));
    m_stitchGridRows->setValue(preset.rows);
    m_stitchGridColumns->setValue(preset.columns);
    m_stitchSpacing->setValue(0);
    m_stitchUniformWidth->setChecked(true);

    m_stitchDirection->blockSignals(false);
    m_stitchGridRows->blockSignals(false);
    m_stitchGridColumns->blockSignals(false);
    m_stitchSpacing->blockSignals(false);
    m_stitchUniformWidth->blockSignals(false);
    m_applyingStitchPreset = false;

    validateAndUpdateUI();
}

void PropertyPanel::onStitchAddPreset()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Add Stitch Preset"), tr("Preset name:"),
                                         QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty())
        return;

    StitchPreset p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name;
    p.rows = m_stitchGridRows->value();
    p.columns = m_stitchGridColumns->value();
    p.category = QStringLiteral("自定义");
    p.isBuiltIn = false;
    p.description = QStringLiteral("%1 x %2").arg(p.rows).arg(p.columns);

    if (StitchPresetManager::instance().savePreset(p)) {
        loadStitchPresets();
        m_currentStitchPresetId = p.id;
        selectStitchPresetById(p.id);
        applyStitchPreset(p);
        emit settingsChanged();
    }
}

void PropertyPanel::onStitchBgColorClicked()
{
    QColor c = QColorDialog::getColor(QColor(m_stitchBgColorLabel->text()), this, tr("Select Background Color"));
    if (!c.isValid())
        return;
    m_stitchBgColorBtn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid gray;").arg(c.name()));
    m_stitchBgColorLabel->setText(c.name().toUpper());
    emit settingsChanged();
}

void PropertyPanel::onStitchOutputDirChanged()
{
    if (!m_stitchOutputDir || !m_stitchOutputCreateBtn)
        return;
    QString path = m_stitchOutputDir->currentText();
    bool exists = QFileInfo::exists(path);
    m_stitchOutputCreateBtn->setVisible(!exists && !path.isEmpty());
    validateAndUpdateUI();
}

void PropertyPanel::onStitchCreateOutputDir()
{
    QString path = m_stitchOutputDir->currentText();
    if (path.isEmpty())
        return;
    if (QDir().mkpath(path)) {
        addOutputHistory(path);
        onStitchOutputDirChanged();
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Failed to create output directory."));
    }
}

void PropertyPanel::onStitchFileNameTemplateChanged()
{
    if (m_stitchFileNamePreview)
        m_stitchFileNamePreview->setText(previewFileNameFromTemplate(m_stitchFileNameTemplate->text()));
    validateAndUpdateUI();
}

void PropertyPanel::onStitchOutputFormatChanged(int index)
{
    Q_UNUSED(index)
    if (!m_stitchOutputFormat || !m_stitchQuality)
        return;
    QString fmt = m_stitchOutputFormat->currentText().toLower();
    if (fmt == QStringLiteral("png"))
        m_stitchQuality->setValue(100);
    else if (fmt == QStringLiteral("jpg") || fmt == QStringLiteral("webp"))
        m_stitchQuality->setValue(90);
}

QString PropertyPanel::previewFileNameFromTemplate(const QString& templ) const
{
    QString result = templ;
    QDateTime now = QDateTime::currentDateTime();
    result.replace(QStringLiteral("{name}"), QStringLiteral("wechat_screenshot_001"));
    result.replace(QStringLiteral("{ext}"), QStringLiteral("png"));
    result.replace(QStringLiteral("{time}"), now.toString(QStringLiteral("yyyyMMdd_HHmmss")));
    result.replace(QStringLiteral("{date}"), now.toString(QStringLiteral("yyyy-MM-dd")));
    result.replace(QStringLiteral("{index}"), QStringLiteral("001"));
    result.replace(QStringLiteral("{size}"), QStringLiteral("1920x1080"));
    // 兼容旧版中文占位符
    result.replace(QStringLiteral("{原名}"), QStringLiteral("wechat_screenshot_001"));
    result.replace(QStringLiteral("{扩展}"), QStringLiteral("png"));
    result.replace(QStringLiteral("{时间}"), now.toString(QStringLiteral("yyyyMMdd_HHmmss")));
    result.replace(QStringLiteral("{日期}"), now.toString(QStringLiteral("yyyy-MM-dd")));
    result.replace(QStringLiteral("{序号}"), QStringLiteral("001"));
    result.replace(QStringLiteral("{尺寸}"), QStringLiteral("1920x1080"));
    if (result.isEmpty())
        result = QStringLiteral("wechat_screenshot_001.png");
    else if (!result.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        result += QStringLiteral(".png");
    return tr("Preview: %1").arg(result);
}

void PropertyPanel::addOutputHistory(const QString& path)
{
    if (path.isEmpty())
        return;
    m_outputHistory.removeAll(path);
    m_outputHistory.prepend(path);
    while (m_outputHistory.size() > 5)
        m_outputHistory.removeLast();

    if (m_stitchOutputDir) {
        m_stitchOutputDir->blockSignals(true);
        QString current = m_stitchOutputDir->currentText();
        m_stitchOutputDir->clear();
        m_stitchOutputDir->addItems(m_outputHistory);
        m_stitchOutputDir->setCurrentText(current);
        m_stitchOutputDir->blockSignals(false);
    }
}

QString PropertyPanel::uiStateFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/ui-state.json");
}

void PropertyPanel::saveUiState()
{
    QJsonObject root;
    QJsonObject sections;
    if (m_stitchSectionSettings)
        sections[QStringLiteral("stitchSettings")] = m_stitchSectionSettings->expanded();
    if (m_stitchSectionPresets)
        sections[QStringLiteral("stitchPresets")] = m_stitchSectionPresets->expanded();
    if (m_stitchSectionOutput)
        sections[QStringLiteral("stitchOutput")] = m_stitchSectionOutput->expanded();
    root[QStringLiteral("sections")] = sections;

    QJsonArray history;
    for (const QString& path : m_outputHistory)
        history.append(path);
    root[QStringLiteral("outputHistory")] = history;

    QFile file(uiStateFilePath());
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void PropertyPanel::loadUiState()
{
    QFile file(uiStateFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    QJsonObject root = doc.object();
    QJsonObject sections = root.value(QStringLiteral("sections")).toObject();

    auto applySection = [&sections](CollapsibleSection* section, const QString& key) {
        if (!section)
            return;
        if (sections.contains(key))
            section->setExpanded(sections.value(key).toBool(true));
    };
    applySection(m_stitchSectionSettings, QStringLiteral("stitchSettings"));
    applySection(m_stitchSectionPresets, QStringLiteral("stitchPresets"));
    applySection(m_stitchSectionOutput, QStringLiteral("stitchOutput"));

    QJsonArray history = root.value(QStringLiteral("outputHistory")).toArray();
    for (const QJsonValue& v : history)
        m_outputHistory.append(v.toString());
    while (m_outputHistory.size() > 5)
        m_outputHistory.removeLast();

    if (m_stitchOutputDir) {
        m_stitchOutputDir->blockSignals(true);
        m_stitchOutputDir->addItems(m_outputHistory);
        if (!m_outputHistory.isEmpty())
            m_stitchOutputDir->setCurrentText(m_outputHistory.first());
        m_stitchOutputDir->blockSignals(false);
    }
}

bool PropertyPanel::canWriteToPath(const QString& path) const
{
    if (path.isEmpty())
        return false;
    QFileInfo info(path);
    if (!info.exists()) {
        QDir parentDir = info.dir();
        if (!parentDir.exists())
            return false;
        return QFileInfo(parentDir.absolutePath()).isWritable();
    }
    return info.isDir() && info.isWritable();
}

bool PropertyPanel::validateStitchSettings(QString* errorMessage, QString* warningMessage) const
{
    bool ok = true;
    QStringList errors;
    QStringList warnings;

    int imageCount = m_imageModel ? m_imageModel->validCount() : 0;
    if (imageCount < 2) {
        errors.append(tr("At least 2 images are required"));
        ok = false;
    }

    int spacing = m_stitchSpacing ? m_stitchSpacing->value() : 0;
    if (spacing < 0 || spacing > 200) {
        errors.append(tr("Spacing is out of range"));
        ok = false;
    }

    QString outputDir = m_stitchOutputDir ? m_stitchOutputDir->currentText() : QString();
    if (outputDir.isEmpty()) {
        errors.append(tr("Output directory is empty"));
        ok = false;
    } else {
        if (!QFileInfo::exists(outputDir)) {
            errors.append(tr("Output directory does not exist"));
            ok = false;
        } else if (!canWriteToPath(outputDir)) {
            errors.append(tr("Output directory is not writable"));
            ok = false;
        } else if (outputDir.length() > 260) {
            warnings.append(tr("Output path is too long"));
        }
    }

    QString templ = m_stitchFileNameTemplate ? m_stitchFileNameTemplate->text() : QString();
    if (templ.isEmpty()) {
        errors.append(tr("File name template is empty"));
        ok = false;
    }

    if (m_imageModel && imageCount >= 2 && m_stitchUniformWidth && m_stitchUniformWidth->isChecked()) {
        int firstW = -1;
        bool inconsistent = false;
        int count = m_imageModel->rowCount(QModelIndex());
        for (int i = 0; i < count; ++i) {
            const ImageItem* item = m_imageModel->itemAt(i);
            if (!item || !item->isValid() || item->isHidden())
                continue;
            if (firstW < 0)
                firstW = item->width();
            else if (item->width() != firstW) {
                inconsistent = true;
                break;
            }
        }
        if (inconsistent)
            warnings.append(tr("Image widths are inconsistent"));
    }

    if (errorMessage)
        *errorMessage = errors.join(QStringLiteral("; "));
    if (warningMessage)
        *warningMessage = warnings.join(QStringLiteral("; "));

    return ok;
}

void PropertyPanel::setInputError(QWidget* widget, bool error)
{
    if (!widget)
        return;
    if (error)
        widget->setStyleSheet(QStringLiteral("border: 1px solid #E81123;"));
    else
        widget->setStyleSheet(QString());
}

void PropertyPanel::setInputWarning(QWidget* widget, bool warning)
{
    if (!widget)
        return;
    if (warning)
        widget->setStyleSheet(QStringLiteral("border: 1px solid #FFC107;"));
    else
        widget->setStyleSheet(QString());
}

void PropertyPanel::validateAndUpdateUI()
{
    if (!m_processBtn)
        return;

    ToolType currentTool = static_cast<ToolType>(m_stack->currentIndex());

    if (currentTool != ToolType::Stitch) {
        m_processBtn->setEnabled(true);
        m_processBtn->setToolTip(QString());
        return;
    }

    QString error, warning;
    bool ok = validateStitchSettings(&error, &warning);

    if (m_stitchOutputDir) {
        QString path = m_stitchOutputDir->currentText();
        bool pathError = !path.isEmpty() && (!QFileInfo::exists(path) || !canWriteToPath(path));
        setInputError(m_stitchOutputDir, pathError);
        if (!pathError && path.length() > 260)
            setInputWarning(m_stitchOutputDir, true);
        else if (!pathError)
            setInputWarning(m_stitchOutputDir, false);
    }

    if (m_stitchFileNameTemplate)
        setInputError(m_stitchFileNameTemplate, m_stitchFileNameTemplate->text().isEmpty());

    if (m_stitchSpacing) {
        int spacing = m_stitchSpacing->value();
        setInputError(m_stitchSpacing, spacing < 0 || spacing > 200);
    }

    if (m_stitchValidationLabel) {
        if (!error.isEmpty()) {
            m_stitchValidationLabel->setText(QStringLiteral("🔴 ") + error);
            m_stitchValidationLabel->setStyleSheet(QStringLiteral("color: #E81123; font-size: 12px;"));
        } else if (!warning.isEmpty()) {
            m_stitchValidationLabel->setText(QStringLiteral("⚠️ ") + warning);
            m_stitchValidationLabel->setStyleSheet(QStringLiteral("color: #FFC107; font-size: 12px;"));
        } else {
            m_stitchValidationLabel->setText(QStringLiteral("✅ ") + tr("Ready"));
            m_stitchValidationLabel->setStyleSheet(QStringLiteral("color: #107C10; font-size: 12px;"));
        }
    }

    m_processBtn->setEnabled(ok);
    m_processBtn->setToolTip(ok ? QString() : error);
}

void PropertyPanel::onImageCountChanged(int count)
{
    Q_UNUSED(count)
    refreshStitchPresetAvailability();
    validateAndUpdateUI();
}

void PropertyPanel::loadResizePresetCategories()
{
    m_resizePresets = ResizePresetManager::instance().loadPresets();
    QStringList cats = ResizePresetManager::instance().categories();

    m_resizeCategory->blockSignals(true);
    m_resizeCategory->clear();
    for (const QString& cat : cats)
        m_resizeCategory->addItem(cat, cat);
    m_resizeCategory->blockSignals(false);
}

void PropertyPanel::loadResizePresetsByCategory()
{
    QString category = m_resizeCategory->currentData().toString();
    m_resizePreset->blockSignals(true);
    m_resizePreset->clear();
    for (const auto& p : m_resizePresets) {
        if (p.category == category) {
            QString ratio = aspectRatioString(p.width, p.height);
            QString text = ratio.isEmpty() ? p.name : QStringLiteral("%1 (%2)").arg(p.name).arg(ratio);
            m_resizePreset->addItem(text, p.id);
        }
    }
    m_resizePreset->blockSignals(false);
    onResizePresetChanged(m_resizePreset->currentIndex());
}

void PropertyPanel::applyResizePreset(const ResizePreset& preset)
{
    m_resizeUpdating = true;
    m_resizeWidth->setValue(preset.width);
    m_resizeHeight->setValue(preset.height);
    m_resizeAspectLabel->setText(aspectRatioString(preset.width, preset.height));
    m_resizeUpdating = false;
}

QString PropertyPanel::aspectRatioString(int w, int h) const
{
    if (w <= 0 || h <= 0)
        return QString();
    int gcd = 1;
    int a = w, b = h;
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    gcd = a;
    return QStringLiteral("%1:%2").arg(w / gcd).arg(h / gcd);
}

void PropertyPanel::onResizeModeChanged(int index)
{
    Q_UNUSED(index)
    bool isPreset = (m_resizeMode->currentData().toInt() == static_cast<int>(ResizeMode::Preset));
    bool isPercentage = (m_resizeMode->currentData().toInt() == static_cast<int>(ResizeMode::Percentage));
    m_resizeCategory->setEnabled(isPreset);
    m_resizePreset->setEnabled(isPreset);
    m_resizeAddPresetBtn->setEnabled(isPreset);
    m_resizeDeletePresetBtn->setEnabled(isPreset);
    m_resizeWidth->setEnabled(!isPreset);
    m_resizeHeight->setEnabled(!isPreset);
    m_resizeLockAspect->setEnabled(!isPreset);
    m_resizeAspectLabel->setEnabled(!isPreset);
    m_resizePercentage->setEnabled(isPercentage);

    if (isPreset)
        loadResizePresetsByCategory();
    requestDelayedPreview();
}

void PropertyPanel::onResizeCategoryChanged(int index)
{
    if (m_resizeMode->currentData().toInt() != static_cast<int>(ResizeMode::Preset))
        return;
    if (index >= 0)
        loadResizePresetsByCategory();
}

void PropertyPanel::onResizePresetChanged(int index)
{
    if (index < 0)
        return;
    QString presetId = m_resizePreset->currentData().toString();
    for (const auto& p : m_resizePresets) {
        if (p.id == presetId) {
            applyResizePreset(p);
            requestDelayedPreview();
            break;
        }
    }
}

void PropertyPanel::onResizeWidthChanged(int value)
{
    if (m_resizeUpdating)
        return;
    if (m_resizeLockAspect->isChecked() && m_lastImage.width() > 0) {
        int h = qRound(value * double(m_lastImage.height()) / m_lastImage.width());
        m_resizeUpdating = true;
        m_resizeHeight->setValue(h);
        m_resizeUpdating = false;
    }
    m_resizeAspectLabel->setText(aspectRatioString(m_resizeWidth->value(), m_resizeHeight->value()));
    requestDelayedPreview();
}

void PropertyPanel::onResizeHeightChanged(int value)
{
    if (m_resizeUpdating)
        return;
    if (m_resizeLockAspect->isChecked() && m_lastImage.height() > 0) {
        int w = qRound(value * double(m_lastImage.width()) / m_lastImage.height());
        m_resizeUpdating = true;
        m_resizeWidth->setValue(w);
        m_resizeUpdating = false;
    }
    m_resizeAspectLabel->setText(aspectRatioString(m_resizeWidth->value(), m_resizeHeight->value()));
    requestDelayedPreview();
}

void PropertyPanel::onResizeAddPreset()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Add Custom Preset"), tr("Preset name:"),
                                         QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty())
        return;

    bool catOk = false;
    QString category = QInputDialog::getItem(this, tr("Add Custom Preset"), tr("Category:"),
                                             ResizePresetManager::instance().categories(),
                                             0, true, &catOk);
    if (!catOk || category.isEmpty())
        category = QStringLiteral("Custom");

    ResizePreset p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name;
    p.width = m_resizeWidth->value();
    p.height = m_resizeHeight->value();
    p.category = category;
    p.isBuiltIn = false;
    p.description = aspectRatioString(p.width, p.height);

    if (ResizePresetManager::instance().savePreset(p)) {
        loadResizePresetCategories();
        m_resizeCategory->setCurrentText(category);
        loadResizePresetsByCategory();
        int idx = m_resizePreset->findData(p.id);
        if (idx >= 0)
            m_resizePreset->setCurrentIndex(idx);
    }
}

void PropertyPanel::onResizeDeletePreset()
{
    int idx = m_resizePreset->currentIndex();
    if (idx < 0)
        return;
    QString presetId = m_resizePreset->currentData().toString();
    for (const auto& p : m_resizePresets) {
        if (p.id == presetId && p.isBuiltIn) {
            QMessageBox::warning(this, tr("Warning"), tr("Cannot delete built-in presets."));
            return;
        }
    }
    if (QMessageBox::question(this, tr("Confirm"), tr("Delete this preset?")) == QMessageBox::Yes) {
        if (ResizePresetManager::instance().deletePreset(presetId)) {
            loadResizePresetCategories();
            loadResizePresetsByCategory();
        }
    }
}

void PropertyPanel::onCompressModeChanged(int index)
{
    Q_UNUSED(index)
    CompressMode mode = static_cast<CompressMode>(m_compressMode->currentData().toInt());
    bool isSizeMode = (mode == CompressMode::Size);
    bool isQuality = (mode == CompressMode::Quality);
    m_compressStrength->setEnabled(!isQuality);
    m_compressQuality->setEnabled(isQuality || mode == CompressMode::Smart);
    m_compressScale->setEnabled(mode == CompressMode::Size || mode == CompressMode::Smart);
    m_compressTargetSize->setEnabled(isSizeMode || mode == CompressMode::Smart);
    m_compressTargetUnit->setEnabled(isSizeMode || mode == CompressMode::Smart);
}

void PropertyPanel::onConvertFormatChanged(int index)
{
    Q_UNUSED(index)
    if (m_convertEstimateTimer)
        m_convertEstimateTimer->start();
}

void PropertyPanel::onConvertEstimateTimeout()
{
    if (m_lastImage.isNull()) {
        m_convertEstimateLabel->setText(QString());
        return;
    }
    qint64 bytes = ConvertEngine::estimateSize(m_lastImage, convertSettings());
    m_convertEstimateLabel->setText(tr("Estimated size: %1").arg(FileUtils::formatFileSize(bytes)));
}

void PropertyPanel::onWatermarkColorClicked()
{
    QColor c = QColorDialog::getColor(m_watermarkColor, this, tr("Select Watermark Color"));
    if (!c.isValid())
        return;
    m_watermarkColor = c;
    m_watermarkColorBtn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid gray;").arg(c.name()));
    emit settingsChanged();
}

void PropertyPanel::onWatermarkTypeChanged(int index)
{
    Q_UNUSED(index)
    bool isText = (m_watermarkType->currentData().toInt() == static_cast<int>(WatermarkType::Text));
    m_watermarkText->setEnabled(isText);
    m_watermarkFontFamily->setEnabled(isText);
    m_watermarkFontSize->setEnabled(isText);
    m_watermarkColorBtn->setEnabled(isText);
    m_watermarkImagePath->setEnabled(!isText);
    QWidget* imageRow = m_watermarkImagePath->parentWidget();
    if (imageRow)
        imageRow->setEnabled(!isText);
}

void PropertyPanel::requestDelayedPreview()
{
    if (m_resizeDelayTimer)
        m_resizeDelayTimer->start();
}

void PropertyPanel::onCompressEstimateRequested()
{
    if (m_lastImage.isNull()) {
        m_compressEstimateLabel->setText(QString());
        return;
    }
    qint64 bytes = CompressEngine::estimateSize(m_lastImage, compressSettings());
    m_compressEstimateLabel->setText(tr("Estimated size: %1").arg(FileUtils::formatFileSize(bytes)));
}

void PropertyPanel::onEditToolChanged(int index)
{
    Q_UNUSED(index)
    EditToolType tool = static_cast<EditToolType>(m_editTool->currentData().toInt());
    bool isShape = (tool == EditToolType::Rectangle || tool == EditToolType::Ellipse);
    bool isText = (tool == EditToolType::Text);
    bool isFilter = (tool == EditToolType::Filter);

    m_editFilterType->setEnabled(isFilter);
    if (m_editFilterType->parentWidget())
        m_editFilterType->parentWidget()->setEnabled(isFilter);
    m_editColor->setEnabled(!isFilter);
    if (m_editColor->parentWidget())
        m_editColor->parentWidget()->setEnabled(!isFilter);
    m_editFillStyle->setEnabled(isShape);
    if (m_editFillStyle->parentWidget())
        m_editFillStyle->parentWidget()->setEnabled(isShape);
    m_editFontSize->setEnabled(isText);
    if (m_editFontSize->parentWidget())
        m_editFontSize->parentWidget()->setEnabled(isText);
    m_editLineWidth->setEnabled(!isFilter);
    if (m_editLineWidth->parentWidget())
        m_editLineWidth->parentWidget()->setEnabled(!isFilter);
    m_editOpacity->setEnabled(!isFilter);
    if (m_editOpacity->parentWidget())
        m_editOpacity->parentWidget()->setEnabled(!isFilter);
}

void PropertyPanel::onEditUndo()
{
    emit editUndoRequested();
}

void PropertyPanel::onEditRedo()
{
    emit editRedoRequested();
}

void PropertyPanel::onEditClear()
{
    emit editClearRequested();
}

void PropertyPanel::onEditHistoryItemClicked(QListWidgetItem* item)
{
    if (!item)
        return;
    bool ok = false;
    int index = item->data(Qt::UserRole).toInt(&ok);
    if (ok)
        emit editHistoryJumpRequested(index);
}

void PropertyPanel::onEditActionAdded(const EditAction& action)
{
    Q_UNUSED(action)
}

void PropertyPanel::refreshEditHistory(const QList<EditAction>& history, int currentIndex)
{
    m_editHistoryList->clear();
    m_editHistoryIndex = currentIndex;
    for (int i = 0; i < history.size(); ++i) {
        const EditAction& a = history.at(i);
        QString toolName;
        switch (a.toolType) {
        case EditToolType::Rectangle: toolName = tr("Rectangle"); break;
        case EditToolType::Ellipse: toolName = tr("Ellipse"); break;
        case EditToolType::Arrow: toolName = tr("Arrow"); break;
        case EditToolType::Pen: toolName = tr("Pen"); break;
        case EditToolType::Mosaic: toolName = tr("Mosaic"); break;
        case EditToolType::Text: toolName = tr("Text"); break;
        case EditToolType::Crop: toolName = tr("Crop"); break;
        case EditToolType::Filter: toolName = tr("Filter"); break;
        }
        QString text = QStringLiteral("%1 %2").arg(i + 1).arg(toolName);
        if (a.toolType == EditToolType::Text && !a.text.isEmpty())
            text += QStringLiteral(" - ") + a.text;
        else if (a.toolType == EditToolType::Filter) {
            QString filterName;
            switch (a.filterType) {
            case FilterType::Grayscale: filterName = tr("Grayscale"); break;
            case FilterType::Sepia: filterName = tr("Sepia"); break;
            case FilterType::Warm: filterName = tr("Warm"); break;
            case FilterType::Cool: filterName = tr("Cool"); break;
            case FilterType::HighContrast: filterName = tr("High Contrast"); break;
            case FilterType::Blur: filterName = tr("Blur"); break;
            case FilterType::Sharpen: filterName = tr("Sharpen"); break;
            }
            text += QStringLiteral(" - ") + filterName;
        }
        QListWidgetItem* item = new QListWidgetItem(text, m_editHistoryList);
        item->setData(Qt::UserRole, i);
        if (i == currentIndex)
            item->setBackground(palette().highlight());
    }

    m_editUndoBtn->setEnabled(currentIndex > 0);
    m_editRedoBtn->setEnabled(currentIndex < history.size() - 1);
    m_editClearBtn->setEnabled(!history.isEmpty());
}

void PropertyPanel::onResizeOutputFormatChanged(int index)
{
    Q_UNUSED(index)
    onSettingsChanged();
}

} // namespace yingtu
