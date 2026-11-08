#include "PropertyPanel.h"
#include "OutputSettingsSection.h"
#include "app/ImageProApp.h"
#include "core/ImageListModel.h"
#include "utils/AppPaths.h"
#include "utils/FileUtils.h"
#include "utils/ImageTaskPools.h"
#include "utils/ResizePresetManager.h"
#include "utils/StitchPresetManager.h"
#include "utils/WidgetState.h"
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
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>
#include <QVBoxLayout>

namespace yingtu {

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent)
{
    setMinimumWidth(200);
    setMaximumWidth(260);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    // 图片信息段（所有工具共用，默认收起）：EXIF 拍摄参数异步读取。
    m_infoSection = new CollapsibleSection(tr("Image Info"), this);
    m_infoSection->setExpanded(false);
    QWidget* infoContent = new QWidget(m_infoSection);
    QVBoxLayout* infoLayout = new QVBoxLayout(infoContent);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);
    m_infoText = new QLabel(infoContent);
    m_infoText->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    m_infoText->setWordWrap(true);
    m_infoText->setMinimumWidth(0);
    m_infoText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    infoLayout->addWidget(m_infoText);
    m_infoSection->setContent(infoContent);
    mainLayout->addWidget(m_infoSection);

    m_stack = new QStackedWidget(this);
    buildStitchPanel();
    buildConvertPanel();
    buildCompressPanel();
    buildWatermarkPanel();
    buildEditPanel();
    buildResizePanel();
    buildBatchPanel();
    buildPdfPanel();

    // 面板内容放进滚动区：八个工具面板的控件数量差异很大，
    // 窗口高度不够时（最小 600px）内容会被裁掉且无法访问 ——
    // 拼接与水印面板尤其容易超出。
    // 滚动区只包住设置区，底部「保存」按钮固定可见。
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setWidget(m_stack);
    // 监听视口尺寸变化：内容宽度必须跟随视口，否则宽内容会被裁掉。
    m_scrollArea->viewport()->installEventFilter(this);

    mainLayout->addWidget(m_scrollArea, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);
    // 主按钮文案随工具变化（开始拼接/开始转换/…）：动作是什么一目了然，
    // 不再是一个所有工具共用的「Save」。
    m_processBtn = new QPushButton(actionButtonText(ToolType::Stitch), this);
    m_processBtn->setDefault(true);
    m_processBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btnLayout->addWidget(m_processBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_processBtn, &QPushButton::clicked, this, &PropertyPanel::processRequested);

    // 统一监听所有设置控件变化（拼接预设下拉框由专有槽处理，避免双重触发）
    for (QComboBox* cb : findChildren<QComboBox*>()) {
        if (cb == m_stitchPresetCategory || cb == m_stitchPreset)
            continue;
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                &PropertyPanel::onSettingsChanged);
    }
    for (QSpinBox* sb : findChildren<QSpinBox*>())
        connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this,
                &PropertyPanel::onSettingsChanged);
    for (QSlider* sl : findChildren<QSlider*>())
        connect(sl, &QSlider::valueChanged, this, &PropertyPanel::onSettingsChanged);
    for (QCheckBox* ch : findChildren<QCheckBox*>())
        connect(ch, &QCheckBox::stateChanged, this, &PropertyPanel::onSettingsChanged);
    for (QLineEdit* le : findChildren<QLineEdit*>())
        connect(le, &QLineEdit::textChanged, this, &PropertyPanel::onSettingsChanged);

    // 输出目录变化：拼接目录触发校验；其余工具的目录参与 settings 参数。
    if (m_stitchOutputSection) {
        connect(m_stitchOutputSection, &OutputSettingsSection::directoryEdited, this,
                &PropertyPanel::onStitchOutputDirChanged);
        connect(m_stitchOutputSection, &OutputSettingsSection::fileNameTemplateEdited, this,
                &PropertyPanel::onStitchFileNameTemplateChanged);
    }
    for (OutputSettingsSection* section : m_outputSections) {
        // 目录「提交」（浏览/新建/编辑完成）时记入跨工具共享的历史。
        connect(section, &OutputSettingsSection::directoryCommitted, this, [this, section]() {
            addOutputHistory(section->directory());
        });
        if (section != m_stitchOutputSection) {
            connect(section, &OutputSettingsSection::directoryEdited, this,
                    &PropertyPanel::onSettingsChanged);
        }
    }

    loadUiState();
    loadStitchPresets();
    setToolType(ToolType::Stitch);

    m_initializing = false;
    QTimer::singleShot(0, this, [this]() { validateAndUpdateUI(); });
}

PropertyPanel::~PropertyPanel()
{
    saveUiState();
}

void PropertyPanel::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && !ImageProApp::isLanguageSwitching())
        retranslateUi();
}

void PropertyPanel::retranslateUi()
{
    // 八个工具面板的文本全部由 build*Panel() 里的 tr() 产生，
    // 整体重建即可换成新语言 —— 逐条维护「控件 ↔ 源串」在两千行里极易漏。
    //
    // 重建前先快照控件取值，重建后原样还原：否则一次语言切换
    // 就会把用户刚调好的方向/间距/质量等参数全部清空。
    const QList<WidgetValue> values = captureWidgetValues(m_stack);
    const int currentIndex = m_stack->currentIndex();

    // 折叠分组的展开状态也要保留。
    const bool settingsExpanded =
        m_stitchSectionSettings ? m_stitchSectionSettings->expanded() : true;
    const bool presetsExpanded = m_stitchSectionPresets ? m_stitchSectionPresets->expanded() : true;
    const bool outputExpanded = m_stitchSectionOutput ? m_stitchSectionOutput->expanded() : true;

    rebuildPanels();

    // 还原取值期间抑制 settingsChanged：逐项写回会触发一连串预览请求。
    {
        QScopedValueRollback<bool> guard(m_validating, true);
        applyWidgetValues(m_stack, values);
    }

    if (m_stack->count() > 0)
        m_stack->setCurrentIndex(currentIndex);

    if (m_stitchSectionSettings)
        m_stitchSectionSettings->setExpanded(settingsExpanded);
    if (m_stitchSectionPresets)
        m_stitchSectionPresets->setExpanded(presetsExpanded);
    if (m_stitchSectionOutput)
        m_stitchSectionOutput->setExpanded(outputExpanded);

    validateAndUpdateUI();
}

void PropertyPanel::rebuildPanels()
{
    // 摘掉旧页面：先 removeWidget 再 deleteLater，避免重建过程中
    // QStackedWidget 仍指向已被销毁的页面。
    while (m_stack->count() > 0) {
        QWidget* page = m_stack->widget(0);
        m_stack->removeWidget(page);
        page->deleteLater();
    }

    buildStitchPanel();
    buildConvertPanel();
    buildCompressPanel();
    buildWatermarkPanel();
    buildEditPanel();
    buildResizePanel();
    buildBatchPanel();
    buildPdfPanel();
}

void PropertyPanel::setToolType(ToolType tool)
{
    m_stack->setCurrentIndex(static_cast<int>(tool));
    // 主按钮文案跟随工具：动作是什么一目了然。
    if (m_processBtn)
        m_processBtn->setText(actionButtonText(tool));
    validateAndUpdateUI();
}

QString PropertyPanel::recentOutputDirectory() const
{
    return m_outputHistory.isEmpty() ? QString() : m_outputHistory.first();
}

void PropertyPanel::showImageInfo(const QString& filePath)
{
    if (!m_infoText)
        return;
    m_infoPath = filePath;
    const int generation = ++m_infoGeneration;
    if (filePath.isEmpty()) {
        m_infoText->setText(QString());
        return;
    }

    // EXIF 读取放预览池（与预览共享串行化，避免并发读盘）；过期请求丢弃。
    QtConcurrent::run(ImageTaskPools::previewPool(), [this, filePath, generation]() {
        const ExifSummary sum = ImageLoader::readExifSummary(filePath);
        QMetaObject::invokeMethod(
            this,
            [this, sum, generation, filePath]() {
                if (generation != m_infoGeneration.load() || filePath != m_infoPath)
                    return;
                if (!sum.valid) {
                    m_infoText->setText(tr("No camera information (EXIF) in this image"));
                    return;
                }
                QStringList lines;
                if (!sum.camera.isEmpty())
                    lines << tr("Camera: %1").arg(sum.camera);
                if (!sum.dateTime.isEmpty())
                    lines << tr("Taken: %1").arg(sum.dateTime);
                if (!sum.exposure.isEmpty() || !sum.aperture.isEmpty() || !sum.iso.isEmpty())
                    lines << tr("Exposure: %1 s  ·  f/%2  ·  ISO %3")
                                 .arg(sum.exposure.isEmpty() ? QStringLiteral("-") : sum.exposure,
                                      sum.aperture.isEmpty() ? QStringLiteral("-") : sum.aperture,
                                      sum.iso.isEmpty() ? QStringLiteral("-") : sum.iso);
                if (!sum.focalLength.isEmpty())
                    lines << tr("Focal length: %1 mm").arg(sum.focalLength);
                m_infoText->setText(lines.join(QLatin1Char('\n')));
            },
            Qt::QueuedConnection);
    });
}

void PropertyPanel::updateBatchSummary()
{
    if (!m_batchSummary)
        return;
    const auto tool = static_cast<ToolType>(m_batchTargetTool->currentData().toInt());
    QString text;
    switch (tool) {
    case ToolType::Convert:
        text = tr("Convert to %1, quality %2")
                   .arg(m_convertFormat->currentText())
                   .arg(m_convertQuality->value());
        break;
    case ToolType::Compress:
        text = tr("Compress (%1), target %2 KB")
                   .arg(m_compressMode->currentText())
                   .arg(m_compressTargetSize->value(), 0, 'f', 2);
        break;
    case ToolType::Watermark:
        text = tr("Watermark: %1")
                   .arg(m_watermarkType->currentIndex() == 0 && !m_watermarkText->text().isEmpty()
                            ? m_watermarkText->text()
                            : tr("image watermark"));
        break;
    case ToolType::Resize:
        if (m_resizeMode->currentIndex() == 0)
            text = tr("Resize to %1%").arg(m_resizePercentage->value());
        else
            text = tr("Resize to %1x%2").arg(m_resizeWidth->value()).arg(m_resizeHeight->value());
        break;
    default:
        text = tr("Parameters follow the corresponding tool panel");
        break;
    }
    m_batchSummary->setText(text);
}

QString PropertyPanel::actionButtonText(ToolType tool) const
{
    // 每个工具一个动词短语：按钮本身就是「下一步做什么」的答案。
    switch (tool) {
    case ToolType::Stitch:
        return tr("Start Stitching");
    case ToolType::Convert:
        return tr("Start Conversion");
    case ToolType::Compress:
        return tr("Start Compression");
    case ToolType::Watermark:
        return tr("Add Watermark");
    case ToolType::Edit:
        return tr("Save Edits");
    case ToolType::Resize:
        return tr("Start Resizing");
    case ToolType::Batch:
        return tr("Start Batch");
    case ToolType::Pdf:
        return tr("Export PDF");
    case ToolType::None:
        break;
    }
    return tr("Start");
}

void PropertyPanel::setImageModel(ImageListModel* model)
{
    if (m_imageModel == model)
        return;
    if (m_imageModel)
        disconnect(m_imageModel, nullptr, this, nullptr);
    m_imageModel = model;
    if (m_imageModel) {
        connect(m_imageModel, &ImageListModel::countChanged, this,
                &PropertyPanel::onImageCountChanged);
        connect(m_imageModel, &ImageListModel::thumbnailLoadFinished, this,
                &PropertyPanel::validateAndUpdateUI);
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

    // 批量选择操作：全选 / 清空 / 反选（默认不选中任何图片，由用户手动加入拼接）。
    m_stitchBatchBtn = new QPushButton(tr("Batch Select"), settingsContent);
    m_stitchBatchBtn->setToolTip(tr("Batch select images for stitching"));
    QMenu* batchMenu = new QMenu(m_stitchBatchBtn);
    batchMenu->addAction(tr("Select All"), this, &PropertyPanel::stitchSelectAllRequested);
    batchMenu->addAction(tr("Clear Selection"), this,
                         &PropertyPanel::stitchClearSelectionRequested);
    batchMenu->addAction(tr("Invert Selection"), this,
                         &PropertyPanel::stitchInvertSelectionRequested);
    m_stitchBatchBtn->setMenu(batchMenu);
    m_stitchBatchBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    settingsLayout->addWidget(m_stitchBatchBtn);

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

    m_stitchBgColorRowWidget = new QWidget(settingsContent);
    QHBoxLayout* bgColorLayout = new QHBoxLayout(m_stitchBgColorRowWidget);
    bgColorLayout->setContentsMargins(0, 2, 0, 2);
    bgColorLayout->setSpacing(6);
    bgColorLayout->addWidget(new QLabel(tr("Color:"), m_stitchBgColorRowWidget));
    m_stitchBgColorBtn = new QPushButton(m_stitchBgColorRowWidget);
    m_stitchBgColorBtn->setFixedSize(QSize(28, 22));
    m_stitchBgColorBtn->setStyleSheet(
        QStringLiteral("background-color: white; border: 1px solid gray;"));
    connect(m_stitchBgColorBtn, &QPushButton::clicked, this,
            &PropertyPanel::onStitchBgColorClicked);
    bgColorLayout->addWidget(m_stitchBgColorBtn);
    m_stitchBgColorLabel = new QLabel(QStringLiteral("#FFFFFF"), m_stitchBgColorRowWidget);
    bgColorLayout->addWidget(m_stitchBgColorLabel, 1);
    bgColorLayout->addStretch();
    settingsLayout->addWidget(m_stitchBgColorRowWidget);

    m_stitchUniformWidth = new QCheckBox(tr("Uniform Width"), settingsContent);
    m_stitchRemoveWhiteEdges = new QCheckBox(tr("Crop Input Margins"), settingsContent);
    m_stitchAutoCropEdges = new QCheckBox(tr("Crop Output Margins"), settingsContent);
    settingsLayout->addWidget(m_stitchUniformWidth);
    settingsLayout->addWidget(m_stitchRemoveWhiteEdges);
    settingsLayout->addWidget(m_stitchAutoCropEdges);

    m_stitchGridRowWidget = new QWidget(settingsContent);
    QHBoxLayout* gridLayout = new QHBoxLayout(m_stitchGridRowWidget);
    gridLayout->setContentsMargins(0, 2, 0, 2);
    gridLayout->setSpacing(6);
    gridLayout->addWidget(new QLabel(tr("Grid:"), m_stitchGridRowWidget));
    m_stitchGridRows = new QSpinBox(m_stitchGridRowWidget);
    m_stitchGridRows->setRange(1, 20);
    m_stitchGridRows->setValue(1);
    m_stitchGridColumns = new QSpinBox(m_stitchGridRowWidget);
    m_stitchGridColumns->setRange(1, 20);
    m_stitchGridColumns->setValue(1);
    gridLayout->addWidget(m_stitchGridRows);
    gridLayout->addWidget(new QLabel(tr("x"), m_stitchGridRowWidget));
    gridLayout->addWidget(m_stitchGridColumns);
    settingsLayout->addWidget(m_stitchGridRowWidget);

    m_stitchOutputFormat = new QComboBox(settingsContent);
    m_stitchOutputFormat->addItems(QStringList()
                                   << QStringLiteral("PNG") << QStringLiteral("JPG")
                                   << QStringLiteral("WebP") << QStringLiteral("TIFF")
                                   << QStringLiteral("BMP"));
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

    // 删除当前选中预设：内置预设不可删（引擎按稳定 id 拒绝），自定义预设此前
    // 只能加不能删，列表会越积越多。
    m_stitchPresetDeleteBtn = new QPushButton(tr("Delete Preset"), presetsContent);
    m_stitchPresetDeleteBtn->setEnabled(false);
    connect(m_stitchPresetDeleteBtn, &QPushButton::clicked, this,
            &PropertyPanel::onStitchDeletePreset);
    presetsLayout->addWidget(m_stitchPresetDeleteBtn);

    m_stitchSectionPresets->setContent(presetsContent);
    panelLayout->addWidget(m_stitchSectionPresets);

    connect(m_stitchPresetCategory, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onStitchCategoryChanged);
    connect(m_stitchPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onStitchPresetChanged);

    connect(m_stitchDirection, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onStitchDirectionChanged);
    connect(m_stitchBackground, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onStitchBackgroundChanged);

    // 输出设置：统一式样 —— 目录（历史/浏览/创建）+ 文件名模板 + 校验标签。
    // 式样与其余七个工具面板完全一致，用户只需要学一次。
    m_stitchSectionOutput = new CollapsibleSection(tr("Output Settings"), panel);
    m_stitchOutputSection = new OutputSettingsSection(true, m_stitchSectionOutput);
    m_stitchOutputSection->setFileNameTemplate(QStringLiteral("{name}_stitched_{time}"));
    m_stitchSectionOutput->setContent(m_stitchOutputSection);
    panelLayout->addWidget(m_stitchSectionOutput);
    m_outputSections.append(m_stitchOutputSection);

    panelLayout->addStretch();
    m_stack->addWidget(panel);

    connect(m_stitchOutputFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onStitchOutputFormatChanged);
    onStitchFileNameTemplateChanged();
    updateStitchControlVisibility();
}

void PropertyPanel::buildConvertPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 参数设置
    CollapsibleSection* settings = new CollapsibleSection(tr("Convert Settings"), panel);
    QWidget* settingsContent = new QWidget(settings);
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);

    m_convertFormat = new QComboBox(settingsContent);
    m_convertFormat->addItems(QStringList() << QStringLiteral("PNG") << QStringLiteral("JPG")
                                            << QStringLiteral("WebP") << QStringLiteral("GIF")
                                            << QStringLiteral("BMP") << QStringLiteral("TIFF"));
    settingsLayout->addWidget(createFormRow(tr("Target Format:"), m_convertFormat));

    m_convertQuality = new QSlider(Qt::Horizontal, settingsContent);
    m_convertQuality->setRange(1, 100);
    m_convertQuality->setValue(90);
    settingsLayout->addWidget(createFormRow(tr("Quality:"), m_convertQuality));

    m_convertEstimateLabel = new QLabel(settingsContent);
    m_convertEstimateLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    // 估算文本可能较长（如 Estimated size: 1.2 MB），允许被压缩到面板宽度内，
    // 避免把整个面板撑宽导致内容被裁切。
    m_convertEstimateLabel->setMinimumWidth(0);
    m_convertEstimateLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    settingsLayout->addWidget(m_convertEstimateLabel);

    settings->setContent(settingsContent);
    layout->addWidget(settings);

    // 高级选项（低频项，默认收起）
    CollapsibleSection* advanced = new CollapsibleSection(tr("Advanced"), panel);
    advanced->setExpanded(false);
    QWidget* advancedContent = new QWidget(advanced);
    QVBoxLayout* advancedLayout = new QVBoxLayout(advancedContent);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(8);

    m_convertKeepExif = new QCheckBox(tr("Keep EXIF"), advancedContent);
    m_convertKeepExif->setChecked(true);
    advancedLayout->addWidget(m_convertKeepExif);

    m_convertToSRgb = new QCheckBox(tr("Convert to sRGB"), advancedContent);
    advancedLayout->addWidget(m_convertToSRgb);

    advanced->setContent(advancedContent);
    layout->addWidget(advanced);

    // 输出设置（批量时作为默认输出目录；单张仍走「另存为」对话框）
    m_convertOutputSection = new OutputSettingsSection(false, panel);
    layout->addWidget(m_convertOutputSection);
    m_outputSections.append(m_convertOutputSection);

    m_convertEstimateTimer = new QTimer(this);
    m_convertEstimateTimer->setSingleShot(true);
    m_convertEstimateTimer->setInterval(200);
    connect(m_convertEstimateTimer, &QTimer::timeout, this,
            &PropertyPanel::onConvertEstimateTimeout);

    connect(m_convertFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onConvertFormatChanged);
    connect(m_convertQuality, &QSlider::valueChanged, this, &PropertyPanel::onConvertFormatChanged);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildCompressPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 参数设置
    CollapsibleSection* settings = new CollapsibleSection(tr("Compress Settings"), panel);
    QWidget* settingsContent = new QWidget(settings);
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);

    m_compressMode = new QComboBox(settingsContent);
    m_compressMode->addItem(tr("Quality"), static_cast<int>(CompressMode::Quality));
    m_compressMode->addItem(tr("Size"), static_cast<int>(CompressMode::Size));
    m_compressMode->addItem(tr("Smart"), static_cast<int>(CompressMode::Smart));
    settingsLayout->addWidget(createFormRow(tr("Mode:"), m_compressMode));

    m_compressStrength = new QSlider(Qt::Horizontal, settingsContent);
    m_compressStrength->setRange(0, 100);
    m_compressStrength->setValue(50);
    settingsLayout->addWidget(createFormRow(tr("Strength:"), m_compressStrength));

    m_compressQuality = new QSlider(Qt::Horizontal, settingsContent);
    m_compressQuality->setRange(1, 100);
    m_compressQuality->setValue(80);
    settingsLayout->addWidget(createFormRow(tr("Quality:"), m_compressQuality));

    m_compressScale = new QSpinBox(settingsContent);
    m_compressScale->setRange(10, 100);
    m_compressScale->setSuffix(QStringLiteral("%"));
    m_compressScale->setValue(100);
    settingsLayout->addWidget(createFormRow(tr("Scale:"), m_compressScale));

    QWidget* targetSizeRow = new QWidget(settingsContent);
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
    settingsLayout->addWidget(targetSizeRow);

    m_compressEstimateLabel = new QLabel(settingsContent);
    m_compressEstimateLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    // 估算文本可能较长（如 Estimated size: 1.2 MB），允许被压缩到面板宽度内，
    // 避免把整个面板撑宽导致内容被裁切。
    m_compressEstimateLabel->setMinimumWidth(0);
    m_compressEstimateLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    settingsLayout->addWidget(m_compressEstimateLabel);

    settings->setContent(settingsContent);
    layout->addWidget(settings);

    // 高级选项（低频项，默认收起）
    CollapsibleSection* advanced = new CollapsibleSection(tr("Advanced"), panel);
    advanced->setExpanded(false);
    QWidget* advancedContent = new QWidget(advanced);
    QVBoxLayout* advancedLayout = new QVBoxLayout(advancedContent);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(8);

    m_compressShowOriginal = new QCheckBox(tr("Show Original"), advancedContent);
    advancedLayout->addWidget(m_compressShowOriginal);

    advanced->setContent(advancedContent);
    layout->addWidget(advanced);

    // 输出设置：目录 + 输出格式。「压缩时顺带转格式」是高频需求——
    // 引擎的 outputFormat 字段一直存在，此前只是没有控件。
    CollapsibleSection* output = new CollapsibleSection(tr("Output Settings"), panel);
    QWidget* outputContent = new QWidget(output);
    QVBoxLayout* outputLayout = new QVBoxLayout(outputContent);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(8);

    m_compressOutputSection = new OutputSettingsSection(false, outputContent);
    outputLayout->addWidget(m_compressOutputSection);

    m_compressOutputFormat = new QComboBox(outputContent);
    m_compressOutputFormat->addItem(tr("Original"), QStringLiteral("original"));
    m_compressOutputFormat->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_compressOutputFormat->addItem(QStringLiteral("JPG"), QStringLiteral("jpg"));
    m_compressOutputFormat->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    outputLayout->addWidget(createFormRow(tr("Output Format:"), m_compressOutputFormat));

    output->setContent(outputContent);
    layout->addWidget(output);
    m_outputSections.append(m_compressOutputSection);

    connect(m_compressMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onCompressModeChanged);
    onCompressModeChanged(0);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildWatermarkPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 参数设置
    CollapsibleSection* settings = new CollapsibleSection(tr("Watermark Settings"), panel);
    QWidget* settingsContent = new QWidget(settings);
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);

    m_watermarkType = new QComboBox(settingsContent);
    m_watermarkType->addItem(tr("Text"), static_cast<int>(WatermarkType::Text));
    m_watermarkType->addItem(tr("Image"), static_cast<int>(WatermarkType::Image));
    settingsLayout->addWidget(createFormRow(tr("Type:"), m_watermarkType));
    connect(m_watermarkType, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onWatermarkTypeChanged);

    m_watermarkText = new QLineEdit(settingsContent);
    m_watermarkText->setText(QStringLiteral("影图"));
    settingsLayout->addWidget(createFormRow(tr("Text:"), m_watermarkText));

    QWidget* wmImageRow = new QWidget(settingsContent);
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
    settingsLayout->addWidget(wmImageRow);

    // 图片水印缩放（主图宽度百分比）：此前引擎硬编码 1/4 且不可调。
    m_watermarkImageScale = new QSpinBox(settingsContent);
    m_watermarkImageScale->setRange(1, 100);
    m_watermarkImageScale->setSuffix(QStringLiteral("%"));
    m_watermarkImageScale->setValue(25);
    settingsLayout->addWidget(createFormRow(tr("Watermark Size:"), m_watermarkImageScale));

    m_watermarkFontFamily = new QComboBox(settingsContent);
    for (const QString& family : QFontDatabase().families()) {
        if (!family.startsWith(QStringLiteral(".")) && !family.isEmpty())
            m_watermarkFontFamily->addItem(family);
    }
    int defaultFontIdx = m_watermarkFontFamily->findText(QStringLiteral("Microsoft YaHei"));
    if (defaultFontIdx < 0)
        defaultFontIdx = m_watermarkFontFamily->findText(QFont().defaultFamily());
    m_watermarkFontFamily->setCurrentIndex(qMax(0, defaultFontIdx));
    settingsLayout->addWidget(createFormRow(tr("Font Family:"), m_watermarkFontFamily));

    m_watermarkFontSize = new QSpinBox(settingsContent);
    m_watermarkFontSize->setRange(8, 200);
    m_watermarkFontSize->setValue(24);
    settingsLayout->addWidget(createFormRow(tr("Font Size:"), m_watermarkFontSize));

    QWidget* colorRow = new QWidget(settingsContent);
    QHBoxLayout* colorLayout = new QHBoxLayout(colorRow);
    colorLayout->setContentsMargins(0, 2, 0, 2);
    colorLayout->setSpacing(6);
    colorLayout->addWidget(new QLabel(tr("Color:"), colorRow));
    m_watermarkColorBtn = new QPushButton(colorRow);
    m_watermarkColorBtn->setFixedSize(QSize(28, 22));
    m_watermarkColorBtn->setStyleSheet(
        QStringLiteral("background-color: white; border: 1px solid gray;"));
    connect(m_watermarkColorBtn, &QPushButton::clicked, this,
            &PropertyPanel::onWatermarkColorClicked);
    colorLayout->addWidget(m_watermarkColorBtn);
    colorLayout->addStretch();
    settingsLayout->addWidget(colorRow);

    m_watermarkOpacity = new QSlider(Qt::Horizontal, settingsContent);
    m_watermarkOpacity->setRange(0, 100);
    m_watermarkOpacity->setValue(50);
    settingsLayout->addWidget(createFormRow(tr("Opacity:"), m_watermarkOpacity));

    m_watermarkRotation = new QSpinBox(settingsContent);
    m_watermarkRotation->setRange(0, 360);
    m_watermarkRotation->setSuffix(QStringLiteral("°"));
    settingsLayout->addWidget(createFormRow(tr("Rotation:"), m_watermarkRotation));

    m_watermarkPosition = new QComboBox(settingsContent);
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
    settingsLayout->addWidget(createFormRow(tr("Position:"), m_watermarkPosition));

    m_watermarkTile = new QCheckBox(tr("Tile"), settingsContent);
    settingsLayout->addWidget(m_watermarkTile);

    m_watermarkTileSpacing = new QSpinBox(settingsContent);
    m_watermarkTileSpacing->setRange(0, 500);
    m_watermarkTileSpacing->setSuffix(QStringLiteral(" px"));
    m_watermarkTileSpacing->setValue(100);
    settingsLayout->addWidget(createFormRow(tr("Tile Spacing:"), m_watermarkTileSpacing));

    m_watermarkMargin = new QSpinBox(settingsContent);
    m_watermarkMargin->setRange(0, 500);
    m_watermarkMargin->setSuffix(QStringLiteral(" px"));
    m_watermarkMargin->setValue(20);
    settingsLayout->addWidget(createFormRow(tr("Margin:"), m_watermarkMargin));

    settings->setContent(settingsContent);
    layout->addWidget(settings);

    // 输出设置：目录 + 格式 + 质量归为一组 —— 「输出到哪、什么格式」本就是
    // 同一件事，之前散在参数列表末尾。
    CollapsibleSection* output = new CollapsibleSection(tr("Output Settings"), panel);
    QWidget* outputContent = new QWidget(output);
    QVBoxLayout* outputLayout = new QVBoxLayout(outputContent);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(8);

    m_watermarkOutputSection = new OutputSettingsSection(false, outputContent);
    outputLayout->addWidget(m_watermarkOutputSection);

    m_watermarkOutputFormat = new QComboBox(outputContent);
    m_watermarkOutputFormat->addItem(tr("Original"), QStringLiteral("original"));
    m_watermarkOutputFormat->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_watermarkOutputFormat->addItem(QStringLiteral("JPG"), QStringLiteral("jpg"));
    m_watermarkOutputFormat->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    outputLayout->addWidget(createFormRow(tr("Output Format:"), m_watermarkOutputFormat));

    m_watermarkQuality = new QSlider(Qt::Horizontal, outputContent);
    m_watermarkQuality->setRange(1, 100);
    m_watermarkQuality->setValue(90);
    outputLayout->addWidget(createFormRow(tr("Quality:"), m_watermarkQuality));

    output->setContent(outputContent);
    layout->addWidget(output);
    m_outputSections.append(m_watermarkOutputSection);

    onWatermarkTypeChanged(0);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildEditPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 编辑工具
    CollapsibleSection* tools = new CollapsibleSection(tr("Edit Tools"), panel);
    QWidget* toolsContent = new QWidget(tools);
    QVBoxLayout* toolsLayout = new QVBoxLayout(toolsContent);
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(8);

    m_editTool = new QComboBox(toolsContent);
    m_editTool->addItem(tr("Rectangle"), static_cast<int>(EditToolType::Rectangle));
    m_editTool->addItem(tr("Ellipse"), static_cast<int>(EditToolType::Ellipse));
    m_editTool->addItem(tr("Arrow"), static_cast<int>(EditToolType::Arrow));
    m_editTool->addItem(tr("Pen"), static_cast<int>(EditToolType::Pen));
    m_editTool->addItem(tr("Mosaic"), static_cast<int>(EditToolType::Mosaic));
    m_editTool->addItem(tr("Text"), static_cast<int>(EditToolType::Text));
    m_editTool->addItem(tr("Crop"), static_cast<int>(EditToolType::Crop));
    m_editTool->addItem(tr("Filter"), static_cast<int>(EditToolType::Filter));
    toolsLayout->addWidget(createFormRow(tr("Tool:"), m_editTool));

    m_editFilterType = new QComboBox(toolsContent);
    m_editFilterType->addItem(tr("Grayscale"), static_cast<int>(FilterType::Grayscale));
    m_editFilterType->addItem(tr("Sepia"), static_cast<int>(FilterType::Sepia));
    m_editFilterType->addItem(tr("Warm"), static_cast<int>(FilterType::Warm));
    m_editFilterType->addItem(tr("Cool"), static_cast<int>(FilterType::Cool));
    m_editFilterType->addItem(tr("High Contrast"), static_cast<int>(FilterType::HighContrast));
    m_editFilterType->addItem(tr("Blur"), static_cast<int>(FilterType::Blur));
    m_editFilterType->addItem(tr("Sharpen"), static_cast<int>(FilterType::Sharpen));
    toolsLayout->addWidget(createFormRow(tr("Filter:"), m_editFilterType));

    // 滤镜强度：此前 7 种滤镜全部硬编码零参数，用户没有任何可调项。
    m_editFilterStrength = new QSlider(Qt::Horizontal, toolsContent);
    m_editFilterStrength->setRange(0, 100);
    m_editFilterStrength->setValue(50);
    toolsLayout->addWidget(createFormRow(tr("Strength:"), m_editFilterStrength));

    m_editMosaicStyle = new QComboBox(toolsContent);
    m_editMosaicStyle->addItem(tr("Square"), static_cast<int>(MosaicStyle::Square));
    m_editMosaicStyle->addItem(tr("Hexagon"), static_cast<int>(MosaicStyle::Hexagon));
    m_editMosaicStyle->addItem(tr("Circle"), static_cast<int>(MosaicStyle::Circle));
    m_editMosaicStyle->addItem(tr("Blur"), static_cast<int>(MosaicStyle::Blur));
    m_editMosaicStyle->addItem(tr("Mezzotint"), static_cast<int>(MosaicStyle::Mezzotint));
    m_editMosaicStyle->addItem(tr("Color Halftone"), static_cast<int>(MosaicStyle::ColorHalftone));
    toolsLayout->addWidget(createFormRow(tr("Mosaic Style:"), m_editMosaicStyle));

    m_editMosaicSize = new QSpinBox(toolsContent);
    m_editMosaicSize->setRange(4, 200);
    m_editMosaicSize->setValue(20);
    m_editMosaicSize->setSuffix(QStringLiteral(" px"));
    toolsLayout->addWidget(createFormRow(tr("Mosaic Size:"), m_editMosaicSize));

    m_editColor = new QComboBox(toolsContent);
    m_editColor->addItem(tr("Red"), QColor(Qt::red));
    m_editColor->addItem(tr("Blue"), QColor(Qt::blue));
    m_editColor->addItem(tr("Green"), QColor(Qt::green));
    m_editColor->addItem(tr("Yellow"), QColor(Qt::yellow));
    m_editColor->addItem(tr("Black"), QColor(Qt::black));
    m_editColor->addItem(tr("White"), QColor(Qt::white));
    m_editColor->setCurrentIndex(0);
    toolsLayout->addWidget(createFormRow(tr("Color:"), m_editColor));

    m_editLineWidth = new QSpinBox(toolsContent);
    m_editLineWidth->setRange(1, 20);
    m_editLineWidth->setValue(3);
    toolsLayout->addWidget(createFormRow(tr("Line Width:"), m_editLineWidth));

    m_editOpacity = new QSlider(Qt::Horizontal, toolsContent);
    m_editOpacity->setRange(0, 100);
    m_editOpacity->setValue(80);
    toolsLayout->addWidget(createFormRow(tr("Opacity:"), m_editOpacity));

    m_editFillStyle = new QComboBox(toolsContent);
    m_editFillStyle->addItem(tr("No Fill"), static_cast<int>(EditFillStyle::NoFill));
    m_editFillStyle->addItem(tr("Semi Fill"), static_cast<int>(EditFillStyle::SemiFill));
    m_editFillStyle->addItem(tr("Solid Fill"), static_cast<int>(EditFillStyle::SolidFill));
    m_editFillStyle->setCurrentIndex(1);
    toolsLayout->addWidget(createFormRow(tr("Fill Style:"), m_editFillStyle));

    m_editFontFamily = new QComboBox(toolsContent);
    for (const QString& family : QFontDatabase().families()) {
        if (!family.startsWith(QStringLiteral(".")) && !family.isEmpty())
            m_editFontFamily->addItem(family);
    }
    int defaultEditFontIdx = m_editFontFamily->findText(QStringLiteral("Microsoft YaHei"));
    if (defaultEditFontIdx < 0)
        defaultEditFontIdx = m_editFontFamily->findText(QFont().defaultFamily());
    m_editFontFamily->setCurrentIndex(qMax(0, defaultEditFontIdx));
    toolsLayout->addWidget(createFormRow(tr("Font Family:"), m_editFontFamily));

    m_editFontSize = new QSpinBox(toolsContent);
    m_editFontSize->setRange(8, 200);
    m_editFontSize->setValue(16);
    toolsLayout->addWidget(createFormRow(tr("Font Size:"), m_editFontSize));

    m_editFontBold = new QCheckBox(tr("Bold"), toolsContent);
    toolsLayout->addWidget(m_editFontBold);

    // 文字背景框：浅色底图上白字可读。
    m_editTextBackground = new QCheckBox(tr("Text Background"), toolsContent);
    toolsLayout->addWidget(m_editTextBackground);

    // 裁剪比例锁：拖拽角部手柄时保持宽高比（经信号同步到编辑器）。
    m_editCropAspectLock = new QCheckBox(tr("Lock Crop Aspect"), toolsContent);
    connect(m_editCropAspectLock, &QCheckBox::toggled, this,
            &PropertyPanel::cropAspectLockChanged);
    toolsLayout->addWidget(m_editCropAspectLock);

    QWidget* textColorRow = new QWidget(toolsContent);
    QHBoxLayout* textColorLayout = new QHBoxLayout(textColorRow);
    textColorLayout->setContentsMargins(0, 2, 0, 2);
    textColorLayout->setSpacing(6);
    textColorLayout->addWidget(new QLabel(tr("Text Color:"), textColorRow));
    m_editTextColorBtn = new QPushButton(textColorRow);
    m_editTextColorBtn->setFixedSize(QSize(28, 22));
    m_editTextColorBtn->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid gray;")
            .arg(m_editTextColor.name()));
    connect(m_editTextColorBtn, &QPushButton::clicked, this,
            &PropertyPanel::onEditFontColorClicked);
    textColorLayout->addWidget(m_editTextColorBtn);
    textColorLayout->addStretch();
    toolsLayout->addWidget(textColorRow);

    // 保存质量：此前保存编辑图固定 quality=90 且不可调。
    m_editQuality = new QSpinBox(toolsContent);
    m_editQuality->setRange(1, 100);
    m_editQuality->setValue(92);
    toolsLayout->addWidget(createFormRow(tr("Save Quality:"), m_editQuality));

    tools->setContent(toolsContent);
    layout->addWidget(tools);

    // 编辑历史
    CollapsibleSection* history = new CollapsibleSection(tr("History"), panel);
    QWidget* historyContent = new QWidget(history);
    QVBoxLayout* historyLayout = new QVBoxLayout(historyContent);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->setSpacing(8);

    QHBoxLayout* historyBtnLayout = new QHBoxLayout();
    historyBtnLayout->setSpacing(6);
    m_editUndoBtn = new QPushButton(tr("Undo"), historyContent);
    m_editRedoBtn = new QPushButton(tr("Redo"), historyContent);
    m_editClearBtn = new QPushButton(tr("Clear"), historyContent);
    historyBtnLayout->addWidget(m_editUndoBtn);
    historyBtnLayout->addWidget(m_editRedoBtn);
    historyBtnLayout->addWidget(m_editClearBtn);
    historyLayout->addLayout(historyBtnLayout);

    m_editHistoryList = new QListWidget(historyContent);
    m_editHistoryList->setMaximumHeight(160);
    historyLayout->addWidget(m_editHistoryList);

    history->setContent(historyContent);
    layout->addWidget(history);

    connect(m_editUndoBtn, &QPushButton::clicked, this, &PropertyPanel::onEditUndo);
    connect(m_editRedoBtn, &QPushButton::clicked, this, &PropertyPanel::onEditRedo);
    connect(m_editClearBtn, &QPushButton::clicked, this, &PropertyPanel::onEditClear);
    connect(m_editHistoryList, &QListWidget::itemClicked, this,
            &PropertyPanel::onEditHistoryItemClicked);
    connect(m_editTool, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onEditToolChanged);

    onEditToolChanged(0);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildResizePanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 参数设置
    CollapsibleSection* settings = new CollapsibleSection(tr("Resize Settings"), panel);
    QWidget* settingsContent = new QWidget(settings);
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);

    m_resizeMode = new QComboBox(settingsContent);
    m_resizeMode->addItem(tr("Percentage"), static_cast<int>(ResizeMode::Percentage));
    m_resizeMode->addItem(tr("Pixel"), static_cast<int>(ResizeMode::Pixel));
    m_resizeMode->addItem(tr("Preset"), static_cast<int>(ResizeMode::Preset));
    settingsLayout->addWidget(createFormRow(tr("Mode:"), m_resizeMode));

    m_resizePercentage = new QSpinBox(settingsContent);
    m_resizePercentage->setRange(10, 500);
    m_resizePercentage->setSuffix(QStringLiteral("%"));
    m_resizePercentage->setValue(100);
    settingsLayout->addWidget(createFormRow(tr("Percentage:"), m_resizePercentage));

    m_resizeWidth = new QSpinBox(settingsContent);
    m_resizeWidth->setRange(1, 10000);
    m_resizeWidth->setValue(800);
    m_resizeHeight = new QSpinBox(settingsContent);
    m_resizeHeight->setRange(1, 10000);
    m_resizeHeight->setValue(600);

    m_resizeLockAspect = new QToolButton(settingsContent);
    m_resizeLockAspect->setCheckable(true);
    m_resizeLockAspect->setChecked(true);
    m_resizeLockAspect->setText(QStringLiteral("🔒"));
    m_resizeLockAspect->setToolTip(tr("Lock aspect ratio"));

    QHBoxLayout* sizeLayout = new QHBoxLayout();
    sizeLayout->setSpacing(6);
    sizeLayout->addWidget(new QLabel(tr("Size:"), settingsContent));
    sizeLayout->addWidget(m_resizeWidth);
    sizeLayout->addWidget(new QLabel(tr("x"), settingsContent));
    sizeLayout->addWidget(m_resizeHeight);
    sizeLayout->addWidget(m_resizeLockAspect);
    settingsLayout->addLayout(sizeLayout);

    m_resizeAspectLabel = new QLabel(settingsContent);
    m_resizeAspectLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    // 估算文本可能较长（如 Estimated size: 1.2 MB），允许被压缩到面板宽度内，
    // 避免把整个面板撑宽导致内容被裁切。
    m_resizeAspectLabel->setMinimumWidth(0);
    m_resizeAspectLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    settingsLayout->addWidget(m_resizeAspectLabel);

    settings->setContent(settingsContent);
    layout->addWidget(settings);

    // 预设（与拼接面板的 Grid Presets 同款式样）
    CollapsibleSection* presets = new CollapsibleSection(tr("Resize Presets"), panel);
    QWidget* presetsContent = new QWidget(presets);
    QVBoxLayout* presetsLayout = new QVBoxLayout(presetsContent);
    presetsLayout->setContentsMargins(0, 0, 0, 0);
    presetsLayout->setSpacing(8);

    m_resizeCategory = new QComboBox(presetsContent);
    m_resizeCategory->setEnabled(false);
    presetsLayout->addWidget(createFormRow(tr("Category:"), m_resizeCategory));

    m_resizePreset = new QComboBox(presetsContent);
    m_resizePreset->setEnabled(false);
    presetsLayout->addWidget(createFormRow(tr("Preset:"), m_resizePreset));

    QHBoxLayout* presetBtnLayout = new QHBoxLayout();
    presetBtnLayout->setSpacing(6);
    m_resizeAddPresetBtn = new QPushButton(tr("Add"), presetsContent);
    m_resizeDeletePresetBtn = new QPushButton(tr("Delete"), presetsContent);
    m_resizeAddPresetBtn->setEnabled(false);
    m_resizeDeletePresetBtn->setEnabled(false);
    presetBtnLayout->addWidget(m_resizeAddPresetBtn);
    presetBtnLayout->addWidget(m_resizeDeletePresetBtn);
    presetsLayout->addLayout(presetBtnLayout);

    presets->setContent(presetsContent);
    layout->addWidget(presets);

    // 输出设置：目录 + 格式 + 质量
    CollapsibleSection* output = new CollapsibleSection(tr("Output Settings"), panel);
    QWidget* outputContent = new QWidget(output);
    QVBoxLayout* outputLayout = new QVBoxLayout(outputContent);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(8);

    m_resizeOutputSection = new OutputSettingsSection(false, outputContent);
    outputLayout->addWidget(m_resizeOutputSection);

    m_resizeOutputFormat = new QComboBox(outputContent);
    m_resizeOutputFormat->addItem(tr("Original"), QStringLiteral("original"));
    m_resizeOutputFormat->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_resizeOutputFormat->addItem(QStringLiteral("JPG"), QStringLiteral("jpg"));
    m_resizeOutputFormat->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    outputLayout->addWidget(createFormRow(tr("Output Format:"), m_resizeOutputFormat));

    m_resizeQuality = new QSlider(Qt::Horizontal, outputContent);
    m_resizeQuality->setRange(1, 100);
    m_resizeQuality->setValue(90);
    outputLayout->addWidget(createFormRow(tr("Quality:"), m_resizeQuality));

    output->setContent(outputContent);
    layout->addWidget(output);
    m_outputSections.append(m_resizeOutputSection);

    // 高级选项（低频项，默认收起）
    CollapsibleSection* advanced = new CollapsibleSection(tr("Advanced"), panel);
    advanced->setExpanded(false);
    QWidget* advancedContent = new QWidget(advanced);
    QVBoxLayout* advancedLayout = new QVBoxLayout(advancedContent);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(8);

    m_resizeInterpolation = new QComboBox(advancedContent);
    m_resizeInterpolation->addItem(tr("Nearest"), static_cast<int>(Interpolation::Nearest));
    m_resizeInterpolation->addItem(tr("Bilinear"), static_cast<int>(Interpolation::Bilinear));
    m_resizeInterpolation->addItem(tr("Bicubic"), static_cast<int>(Interpolation::Bicubic));
    m_resizeInterpolation->addItem(tr("Lanczos"), static_cast<int>(Interpolation::Lanczos));
    m_resizeInterpolation->setCurrentIndex(1);
    advancedLayout->addWidget(createFormRow(tr("Interpolation:"), m_resizeInterpolation));

    m_resizeFitWithinOriginal = new QCheckBox(tr("Do not exceed original size"), advancedContent);
    advancedLayout->addWidget(m_resizeFitWithinOriginal);

    advanced->setContent(advancedContent);
    layout->addWidget(advanced);

    m_resizeDelayTimer = new QTimer(this);
    m_resizeDelayTimer->setSingleShot(true);
    m_resizeDelayTimer->setInterval(200);
    connect(m_resizeDelayTimer, &QTimer::timeout, this, &PropertyPanel::previewRequested);

    connect(m_resizeMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onResizeModeChanged);
    connect(m_resizeCategory, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onResizeCategoryChanged);
    connect(m_resizePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::onResizePresetChanged);
    connect(m_resizeWidth, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &PropertyPanel::onResizeWidthChanged);
    connect(m_resizeHeight, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &PropertyPanel::onResizeHeightChanged);
    connect(m_resizeLockAspect, &QToolButton::toggled, this, &PropertyPanel::onSettingsChanged);
    connect(m_resizeAddPresetBtn, &QPushButton::clicked, this, &PropertyPanel::onResizeAddPreset);
    connect(m_resizeDeletePresetBtn, &QPushButton::clicked, this,
            &PropertyPanel::onResizeDeletePreset);

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
    layout->setSpacing(10);

    // 参数设置
    CollapsibleSection* settings = new CollapsibleSection(tr("Batch Processing"), panel);
    QWidget* settingsContent = new QWidget(settings);
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);

    m_batchTargetTool = new QComboBox(settingsContent);
    m_batchTargetTool->addItem(tr("Convert"), static_cast<int>(ToolType::Convert));
    m_batchTargetTool->addItem(tr("Compress"), static_cast<int>(ToolType::Compress));
    m_batchTargetTool->addItem(tr("Watermark"), static_cast<int>(ToolType::Watermark));
    m_batchTargetTool->addItem(tr("Resize"), static_cast<int>(ToolType::Resize));
    settingsLayout->addWidget(createFormRow(tr("Target Tool:"), m_batchTargetTool));

    // 参数摘要：批处理直接套用对应工具面板的参数，这里实时显示将应用什么，
    // 用户不必来回切换面板确认。
    m_batchSummary = new QLabel(settingsContent);
    m_batchSummary->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
    m_batchSummary->setWordWrap(true);
    m_batchSummary->setMinimumWidth(0);
    m_batchSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    settingsLayout->addWidget(m_batchSummary);
    // 任一面板参数变化都刷新摘要（settingsChanged 由全部设置控件触发）。
    connect(this, &PropertyPanel::settingsChanged, this,
            &PropertyPanel::updateBatchSummary);
    connect(m_batchTargetTool, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::updateBatchSummary);
    updateBatchSummary();

    settings->setContent(settingsContent);
    layout->addWidget(settings);

    // 输出设置
    m_batchOutputSection = new OutputSettingsSection(false, panel);
    layout->addWidget(m_batchOutputSection);
    m_outputSections.append(m_batchOutputSection);

    layout->addStretch();
    m_stack->addWidget(panel);
}

void PropertyPanel::buildPdfPanel()
{
    QWidget* panel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 参数设置
    CollapsibleSection* settings = new CollapsibleSection(tr("PDF Export"), panel);
    QWidget* settingsContent = new QWidget(settings);
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);

    m_pdfPageSize = new QComboBox(settingsContent);
    m_pdfPageSize->addItem(tr("A4"), static_cast<int>(PdfSettings::A4));
    m_pdfPageSize->addItem(tr("A5"), static_cast<int>(PdfSettings::A5));
    m_pdfPageSize->addItem(tr("Letter"), static_cast<int>(PdfSettings::Letter));
    m_pdfPageSize->addItem(tr("Custom"), static_cast<int>(PdfSettings::Custom));
    settingsLayout->addWidget(createFormRow(tr("Page Size:"), m_pdfPageSize));

    // 自定义页面尺寸（mm）：引擎与 settings 字段一直存在，此前没有控件。
    m_pdfCustomSizeRow = new QWidget(settingsContent);
    QHBoxLayout* customSizeLayout = new QHBoxLayout(m_pdfCustomSizeRow);
    customSizeLayout->setContentsMargins(0, 2, 0, 2);
    customSizeLayout->setSpacing(6);
    customSizeLayout->addWidget(new QLabel(tr("Size (mm):"), m_pdfCustomSizeRow));
    m_pdfCustomWidth = new QDoubleSpinBox(m_pdfCustomSizeRow);
    m_pdfCustomWidth->setRange(10.0, 2000.0);
    m_pdfCustomWidth->setValue(210.0);
    m_pdfCustomWidth->setSuffix(QStringLiteral(" ×"));
    m_pdfCustomHeight = new QDoubleSpinBox(m_pdfCustomSizeRow);
    m_pdfCustomHeight->setRange(10.0, 2000.0);
    m_pdfCustomHeight->setValue(297.0);
    m_pdfCustomHeight->setSuffix(QStringLiteral(" mm"));
    customSizeLayout->addWidget(m_pdfCustomWidth);
    customSizeLayout->addWidget(m_pdfCustomHeight);
    customSizeLayout->addStretch();
    m_pdfCustomSizeRow->setVisible(false);
    settingsLayout->addWidget(m_pdfCustomSizeRow);
    connect(m_pdfPageSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                const bool custom = static_cast<PdfSettings::PageSize>(
                                        m_pdfPageSize->currentData().toInt()) ==
                                    PdfSettings::Custom;
                m_pdfCustomSizeRow->setVisible(custom);
            });

    m_pdfLayout = new QComboBox(settingsContent);
    m_pdfLayout->addItem(tr("Single Per Page"), static_cast<int>(PdfSettings::SinglePerPage));
    m_pdfLayout->addItem(tr("Fit to Page"), static_cast<int>(PdfSettings::FitToPage));
    m_pdfLayout->addItem(tr("Grid 2x2"), static_cast<int>(PdfSettings::Grid2x2));
    m_pdfLayout->addItem(tr("Grid 3x3"), static_cast<int>(PdfSettings::Grid3x3));
    settingsLayout->addWidget(createFormRow(tr("Layout:"), m_pdfLayout));

    m_pdfDpi = new QSpinBox(settingsContent);
    m_pdfDpi->setRange(72, 600);
    m_pdfDpi->setValue(150);
    m_pdfDpi->setSuffix(QStringLiteral(" dpi"));
    settingsLayout->addWidget(createFormRow(tr("Resolution:"), m_pdfDpi));

    QWidget* marginRow = new QWidget(settingsContent);
    QHBoxLayout* marginLayout = new QHBoxLayout(marginRow);
    marginLayout->setContentsMargins(0, 2, 0, 2);
    marginLayout->setSpacing(6);
    marginLayout->addWidget(new QLabel(tr("Margins:"), marginRow));
    m_pdfMarginLeft = new QDoubleSpinBox(marginRow);
    m_pdfMarginTop = new QDoubleSpinBox(marginRow);
    m_pdfMarginRight = new QDoubleSpinBox(marginRow);
    m_pdfMarginBottom = new QDoubleSpinBox(marginRow);
    for (auto* sb : {m_pdfMarginLeft, m_pdfMarginTop, m_pdfMarginRight, m_pdfMarginBottom}) {
        sb->setRange(0, 100);
        sb->setValue(20.0);
        sb->setDecimals(1);
        sb->setSuffix(QStringLiteral(" mm"));
        marginLayout->addWidget(sb);
    }
    settingsLayout->addWidget(marginRow);

    settings->setContent(settingsContent);
    layout->addWidget(settings);

    // 输出设置：PDF 是单文件导出，走「保存文件」而不是目录。
    CollapsibleSection* output = new CollapsibleSection(tr("Output Settings"), panel);
    QWidget* outputContent = new QWidget(output);
    QVBoxLayout* outputLayout = new QVBoxLayout(outputContent);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(8);

    QWidget* outRow = new QWidget(outputContent);
    QHBoxLayout* outLayout = new QHBoxLayout(outRow);
    outLayout->setContentsMargins(0, 2, 0, 2);
    outLayout->setSpacing(6);
    outLayout->addWidget(new QLabel(tr("Output:"), outRow));
    m_pdfOutputPath = new QLineEdit(outRow);
    outLayout->addWidget(m_pdfOutputPath, 1);
    QPushButton* browseBtn = new QPushButton(tr("Browse..."), outRow);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Save PDF"), m_pdfOutputPath->text(),
                                                    tr("PDF Files (*.pdf)"));
        if (!path.isEmpty())
            m_pdfOutputPath->setText(path);
    });
    outLayout->addWidget(browseBtn);
    outputLayout->addWidget(outRow);

    output->setContent(outputContent);
    layout->addWidget(output);

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
    // 统一尺寸复选框按方向映射：Vertical→uniformWidth，Horizontal→uniformHeight，
    // Grid→两者皆不需要（Grid 自身按最大单元格尺寸缩放）。
    s.uniformWidth = (s.direction == StitchSettings::Vertical) && m_stitchUniformWidth &&
                     m_stitchUniformWidth->isChecked();
    s.uniformHeight = (s.direction == StitchSettings::Horizontal) && m_stitchUniformWidth &&
                      m_stitchUniformWidth->isChecked();
    s.removeWhiteEdges = m_stitchRemoveWhiteEdges->isChecked();
    s.autoCropEdges = m_stitchAutoCropEdges->isChecked();
    s.gridRows = m_stitchGridRows->value();
    s.gridColumns = m_stitchGridColumns->value();
    s.outputFormat = m_stitchOutputFormat->currentText().toLower();
    s.quality = m_stitchQuality->value();
    s.outputDir = m_stitchOutputSection ? m_stitchOutputSection->directory() : QString();
    return s;
}

ConvertSettings PropertyPanel::convertSettings() const
{
    ConvertSettings s;
    s.targetFormat = m_convertFormat->currentText().toLower();
    s.quality = m_convertQuality->value();
    s.keepExif = m_convertKeepExif->isChecked();
    s.convertToSRgb = m_convertToSRgb->isChecked();
    s.outputDir = m_convertOutputSection ? m_convertOutputSection->directory() : QString();
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
    s.outputFormat = m_compressOutputFormat ? m_compressOutputFormat->currentData().toString()
                                            : QStringLiteral("original");
    s.outputDir = m_compressOutputSection ? m_compressOutputSection->directory() : QString();
    return s;
}

WatermarkSettings PropertyPanel::watermarkSettings() const
{
    WatermarkSettings s;
    s.type = static_cast<WatermarkType>(m_watermarkType->currentData().toInt());
    s.text = m_watermarkText->text();
    s.imagePath = m_watermarkImagePath->text();
    s.imageScalePercent = m_watermarkImageScale ? m_watermarkImageScale->value() : 25;
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
    s.outputDir = m_watermarkOutputSection ? m_watermarkOutputSection->directory() : QString();
    return s;
}

EditAction PropertyPanel::currentEditAction() const
{
    EditAction a;
    a.toolType = static_cast<EditToolType>(m_editTool->currentData().toInt());
    a.filterType = static_cast<FilterType>(m_editFilterType->currentData().toInt());
    a.filterStrength = m_editFilterStrength ? m_editFilterStrength->value() : 50;
    a.mosaicStyle = static_cast<MosaicStyle>(m_editMosaicStyle->currentData().toInt());
    a.mosaicSize = m_editMosaicSize->value();
    a.lineWidth = m_editLineWidth->value();
    a.opacity = m_editOpacity->value();
    a.fontFamily = m_editFontFamily->currentText();
    a.fontSize = m_editFontSize->value();
    a.fontBold = m_editFontBold->isChecked();
    a.textBackground = m_editTextBackground ? m_editTextBackground->isChecked() : false;
    if (a.toolType == EditToolType::Text)
        a.color = m_editTextColor;
    else
        a.color = m_editColor->currentData().value<QColor>();
    a.fillStyle = static_cast<EditFillStyle>(m_editFillStyle->currentData().toInt());
    return a;
}

int PropertyPanel::editQuality() const
{
    return m_editQuality ? m_editQuality->value() : 92;
}

PdfSettings PropertyPanel::pdfSettings() const
{
    PdfSettings s;
    s.pageSize = static_cast<PdfSettings::PageSize>(m_pdfPageSize->currentData().toInt());
    if (s.pageSize == PdfSettings::Custom && m_pdfCustomWidth && m_pdfCustomHeight) {
        // PageSizeMm 对 Custom 有默认兜底，但显式带上用户输入的尺寸。
        s.customPageSize = QSizeF(m_pdfCustomWidth->value(), m_pdfCustomHeight->value());
    }
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
    s.outputDir = m_resizeOutputSection ? m_resizeOutputSection->directory() : QString();

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
    return m_stitchOutputSection ? m_stitchOutputSection->fileNameTemplate() : QString();
}

PropertyPanel::BatchSettings PropertyPanel::batchSettings() const
{
    BatchSettings settings;
    settings.targetTool = static_cast<ToolType>(m_batchTargetTool->currentData().toInt());
    settings.outputDir = m_batchOutputSection ? m_batchOutputSection->directory() : QString();
    return settings;
}

void PropertyPanel::updatePreview(const QImage& image, const QSize& sourceSize)
{
    m_lastImage = image;
    // 原图尺寸随预览一并传来：体积估算此前基于预览图尺寸（≤3840 长边），
    // 对大图会严重低估。按像素数比例放大估算值。
    if (sourceSize.isValid())
        m_lastSourceSize = sourceSize;

    // 刷新压缩体积预估。此前 onCompressEstimateRequested 已实现，却没有任何
    // 地方连接它，导致 m_compressEstimateLabel 始终为空。
    // 挂在这里而不是 onSettingsChanged：estimateSize 最多做 8 次编码，
    // 跟着每次 spinbox 变动跑会明显卡顿；而 updatePreview 已被 MainWindow
    // 的预览防抖（200ms 定时器）节流，频率天然合适。
    if (m_stack && m_stack->currentIndex() == static_cast<int>(ToolType::Compress))
        onCompressEstimateRequested();
}

void PropertyPanel::onSettingsChanged()
{
    if (m_initializing || m_validating)
        return;

    if (!m_applyingStitchPreset && m_stack->currentIndex() == static_cast<int>(ToolType::Stitch)) {
        if (!m_currentStitchPresetId.isEmpty())
            clearStitchPresetSelection();
    }

    emit settingsChanged();
    validateAndUpdateUI();
}

bool PropertyPanel::eventFilter(QObject* watched, QEvent* event)
{
    // 滚动区里的内容宽度必须跟随视口。
    //
    // QScrollArea 的 widgetResizable 只有在「widget 尺寸已等于视口」时才会去设置
    // widget 的最大宽度；一旦两者恰好相等，这条路径就不会执行，widget 会停留在
    // 自己的 sizeHint 上。而 QStackedWidget 的 sizeHint 由最长的一行内容决定
    // （例如带时间戳的拼接文件名可达 300+ px），于是内容比面板宽，
    // 右侧被裁掉且无法看到。
    //
    // 这里挂在视口的 resize 上而不是面板的 resize 上：面板尺寸变化时，
    // 视口宽度是在布局激活之后才更新的，监听面板会拿到旧值。
    if (m_scrollArea && watched == m_scrollArea->viewport() && m_stack) {
        const int viewportWidth = m_scrollArea->viewport()->width();
        if (viewportWidth > 0 && m_stack->width() != viewportWidth)
            m_stack->setFixedWidth(viewportWidth);
        // 宽度变化后重新计算省略文本的截断位置。
        updateStitchFileNamePreview();
    }
    return QWidget::eventFilter(watched, event);
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
    // 方向缩写：V=垂直, H=水平, Grid=行列数
    QString dirStr;
    if (preset.direction == static_cast<int>(StitchSettings::Vertical))
        dirStr = tr("V");
    else if (preset.direction == static_cast<int>(StitchSettings::Horizontal))
        dirStr = tr("H");
    else
        dirStr = QStringLiteral("%1x%2").arg(preset.rows).arg(preset.columns);
    return QStringLiteral("%1 (%2)").arg(preset.name).arg(dirStr);
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
    if (m_initializing)
        return;
    rebuildStitchPresetDropdown();
}

void PropertyPanel::onStitchPresetChanged(int index)
{
    if (m_initializing || m_rebuildingStitchPresets || m_applyingStitchPreset)
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
            break;
        }
    }
    m_stitchPreset->setCurrentIndex(0);
    updateStitchPresetButtons();
}

void PropertyPanel::clearStitchPresetSelection()
{
    m_currentStitchPresetId.clear();
    if (m_stitchPreset && m_stitchPreset->currentIndex() != 0)
        m_stitchPreset->setCurrentIndex(0);
    updateStitchPresetButtons();
}

void PropertyPanel::updateStitchPresetButtons()
{
    // 仅自定义预设可删除（内置项由引擎按稳定 id 保护）。
    bool deletable = false;
    for (const StitchPreset& p : m_stitchPresets) {
        if (p.id == m_currentStitchPresetId) {
            deletable = !p.isBuiltIn;
            break;
        }
    }
    if (m_stitchPresetDeleteBtn)
        m_stitchPresetDeleteBtn->setEnabled(deletable);
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

    // 使用预设自带的方向；旧数据无方向字段时默认 Grid（兼容）
    const int dir = preset.direction;
    m_stitchDirection->setCurrentIndex(m_stitchDirection->findData(dir >= 0 && dir <= 2 ? dir : 2));

    if (dir == static_cast<int>(StitchSettings::Grid)) {
        m_stitchGridRows->setValue(preset.rows);
        m_stitchGridColumns->setValue(preset.columns);
    }
    m_stitchSpacing->setValue(0);
    m_stitchUniformWidth->setChecked(true);

    m_stitchDirection->blockSignals(false);
    m_stitchGridRows->blockSignals(false);
    m_stitchGridColumns->blockSignals(false);
    m_stitchSpacing->blockSignals(false);
    m_stitchUniformWidth->blockSignals(false);
    m_applyingStitchPreset = false;

    updateStitchControlVisibility();
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
    p.direction = m_stitchDirection->currentData().toInt();
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

void PropertyPanel::onStitchDeletePreset()
{
    if (m_currentStitchPresetId.isEmpty())
        return;
    if (!StitchPresetManager::instance().deletePreset(m_currentStitchPresetId)) {
        QMessageBox::warning(this, tr("Error"), tr("Built-in presets cannot be deleted."));
        return;
    }
    m_currentStitchPresetId.clear();
    loadStitchPresets();
    clearStitchPresetSelection();
    emit settingsChanged();
}

void PropertyPanel::onStitchBgColorClicked()
{
    QColor c = QColorDialog::getColor(QColor(m_stitchBgColorLabel->text()), this,
                                      tr("Select Background Color"));
    if (!c.isValid())
        return;
    m_stitchBgColorBtn->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid gray;").arg(c.name()));
    m_stitchBgColorLabel->setText(c.name().toUpper());
    emit settingsChanged();
}

void PropertyPanel::onStitchOutputDirChanged()
{
    // 目录变化只需重新校验；「创建目录」按钮的显隐由 OutputSettingsSection 自理。
    validateAndUpdateUI();
}

void PropertyPanel::onStitchFileNameTemplateChanged()
{
    updateStitchFileNamePreview();
    validateAndUpdateUI();
}

void PropertyPanel::updateStitchFileNamePreview()
{
    if (!m_stitchOutputSection)
        return;

    const QString full = previewFileNameFromTemplate(m_stitchOutputSection->fileNameTemplate());
    // 完整文本放进 tooltip，标签本身按可用宽度做中间省略（OutputSettingsSection
    // 内部处理），保证任何宽度下都完整可见，且不会把面板撑宽。
    m_stitchOutputSection->setTemplatePreview(full);
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

void PropertyPanel::onStitchDirectionChanged(int index)
{
    Q_UNUSED(index)
    if (m_initializing)
        return;
    updateStitchControlVisibility();
    // 方向切换后清除预设选择（预设与方向绑定）
    if (!m_currentStitchPresetId.isEmpty())
        clearStitchPresetSelection();
    emit settingsChanged();
}

void PropertyPanel::onStitchBackgroundChanged(int index)
{
    Q_UNUSED(index)
    if (m_initializing)
        return;
    updateStitchControlVisibility();
    emit settingsChanged();
}

void PropertyPanel::updateStitchControlVisibility()
{
    if (!m_stitchDirection)
        return;

    const auto dir =
        static_cast<StitchSettings::Direction>(m_stitchDirection->currentData().toInt());
    const bool isGrid = (dir == StitchSettings::Grid);

    // Grid 行/列仅在 Grid 方向显示
    if (m_stitchGridRowWidget)
        m_stitchGridRowWidget->setVisible(isGrid);

    // 统一尺寸复选框：Grid 时隐藏（Grid 自带统一单元格缩放），
    // Vertical 显示"Uniform Width"，Horizontal 显示"Uniform Height"。
    if (m_stitchUniformWidth) {
        m_stitchUniformWidth->setVisible(!isGrid);
        if (dir == StitchSettings::Vertical)
            m_stitchUniformWidth->setText(tr("Uniform Width"));
        else if (dir == StitchSettings::Horizontal)
            m_stitchUniformWidth->setText(tr("Uniform Height"));
    }

    // 背景色行仅在非透明背景时显示
    if (m_stitchBgColorRowWidget && m_stitchBackground) {
        const bool transparent =
            (m_stitchBackground->currentData().toString() == QStringLiteral("transparent"));
        m_stitchBgColorRowWidget->setVisible(!transparent);
    }
}

QString PropertyPanel::previewFileNameFromTemplate(const QString& templ) const
{
    // 占位符展开逻辑与 FileUtils::applyFileNameTemplate 共用一套语义，
    // 预览里看到的名字就是实际输出名。
    const QString expanded = FileUtils::applyFileNameTemplate(
        templ, QStringLiteral("wechat_screenshot_001.png"), 1, QSize(1920, 1080),
        QStringLiteral("stitched"));
    return tr("Preview: %1").arg(expanded + QStringLiteral(".png"));
}

void PropertyPanel::addOutputHistory(const QString& path)
{
    if (path.isEmpty())
        return;
    m_outputHistory.removeAll(path);
    m_outputHistory.prepend(path);
    while (m_outputHistory.size() > 5)
        m_outputHistory.removeLast();

    // 跨工具共享：任一工具里选过的输出目录，其余工具的下拉框里都能看到。
    for (OutputSettingsSection* section : m_outputSections)
        section->setHistory(m_outputHistory);
}

QString PropertyPanel::uiStateFilePath() const
{
    QString dir = appConfigDir();
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
    for (const auto& v : history)
        m_outputHistory.append(v.toString());
    while (m_outputHistory.size() > 5)
        m_outputHistory.removeLast();

    for (OutputSettingsSection* section : m_outputSections) {
        section->setHistory(m_outputHistory);
        if (!m_outputHistory.isEmpty())
            section->setDirectory(m_outputHistory.first());
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

    QString outputDir = m_stitchOutputSection ? m_stitchOutputSection->directory() : QString();
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

    QString templ =
        m_stitchOutputSection ? m_stitchOutputSection->fileNameTemplate() : QString();
    if (templ.isEmpty()) {
        errors.append(tr("File name template is empty"));
        ok = false;
    }

    // Grid 图片数与单元格数不匹配时给出警告
    if (m_stitchDirection && m_imageModel && imageCount >= 2) {
        const auto dir =
            static_cast<StitchSettings::Direction>(m_stitchDirection->currentData().toInt());
        if (dir == StitchSettings::Grid && m_stitchGridRows && m_stitchGridColumns) {
            const int cells = m_stitchGridRows->value() * m_stitchGridColumns->value();
            if (imageCount < cells)
                warnings.append(
                    tr("%1 of %2 grid cells will be empty").arg(cells - imageCount).arg(cells));
            else if (imageCount > cells)
                warnings.append(tr("%1 extra image(s) will be ignored").arg(imageCount - cells));
        }
    }

    // 统一尺寸警告（方向感知）
    if (m_imageModel && imageCount >= 2 && m_stitchUniformWidth &&
        m_stitchUniformWidth->isChecked() && m_stitchUniformWidth->isVisible()) {
        const auto dir =
            static_cast<StitchSettings::Direction>(m_stitchDirection->currentData().toInt());
        const bool checkWidth = (dir == StitchSettings::Vertical);
        int firstVal = -1;
        bool inconsistent = false;
        int count = m_imageModel->rowCount(QModelIndex());
        for (int i = 0; i < count; ++i) {
            const ImageItem* item = m_imageModel->itemAt(i);
            if (!item || !item->isValid() || item->isHidden())
                continue;
            const int val = checkWidth ? item->width() : item->height();
            if (firstVal < 0)
                firstVal = val;
            else if (val != firstVal) {
                inconsistent = true;
                break;
            }
        }
        if (inconsistent)
            warnings.append(checkWidth ? tr("Image widths are inconsistent")
                                       : tr("Image heights are inconsistent"));
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
    if (!m_processBtn || m_validating)
        return;

    QScopedValueRollback<bool> guard(m_validating, true);
    ToolType currentTool = static_cast<ToolType>(m_stack->currentIndex());

    if (currentTool != ToolType::Stitch) {
        m_processBtn->setEnabled(true);
        m_processBtn->setToolTip(QString());
        return;
    }

    QString error, warning;
    bool ok = validateStitchSettings(&error, &warning);

    if (m_stitchOutputSection) {
        QString path = m_stitchOutputSection->directory();
        bool pathError = !path.isEmpty() && (!QFileInfo::exists(path) || !canWriteToPath(path));
        setInputError(m_stitchOutputSection->dirCombo(), pathError);
        if (!pathError && path.length() > 260)
            setInputWarning(m_stitchOutputSection->dirCombo(), true);
        else if (!pathError)
            setInputWarning(m_stitchOutputSection->dirCombo(), false);
    }

    if (m_stitchOutputSection && m_stitchOutputSection->fileNameTemplateEdit())
        setInputError(m_stitchOutputSection->fileNameTemplateEdit(),
                      m_stitchOutputSection->fileNameTemplate().isEmpty());

    if (m_stitchSpacing) {
        int spacing = m_stitchSpacing->value();
        setInputError(m_stitchSpacing, spacing < 0 || spacing > 200);
    }

    if (m_stitchOutputSection) {
        if (!error.isEmpty())
            m_stitchOutputSection->setValidationText(QStringLiteral("🔴 ") + error, true, false);
        else if (!warning.isEmpty())
            m_stitchOutputSection->setValidationText(QStringLiteral("⚠️ ") + warning, false, true);
        else
            m_stitchOutputSection->setValidationText(QStringLiteral("✅ ") + tr("Ready"), false,
                                                     false);
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
            QString text =
                ratio.isEmpty() ? p.name : QStringLiteral("%1 (%2)").arg(p.name).arg(ratio);
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
    bool isPercentage =
        (m_resizeMode->currentData().toInt() == static_cast<int>(ResizeMode::Percentage));
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
    m_resizeAspectLabel->setText(
        aspectRatioString(m_resizeWidth->value(), m_resizeHeight->value()));
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
    m_resizeAspectLabel->setText(
        aspectRatioString(m_resizeWidth->value(), m_resizeHeight->value()));
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
    QString category =
        QInputDialog::getItem(this, tr("Add Custom Preset"), tr("Category:"),
                              ResizePresetManager::instance().categories(), 0, true, &catOk);
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
    // Quality 模式：Strength 是主控（引擎 quality = 100 − strength），Quality 滑块禁用；
    // Size/Smart 模式：目标体积 + 缩放生效，Quality 滑块作二分起始质量。
    const bool isQuality = (mode == CompressMode::Quality);
    const bool sizeLike = (mode == CompressMode::Size || mode == CompressMode::Smart);
    m_compressStrength->setEnabled(isQuality);
    m_compressQuality->setEnabled(!isQuality);
    m_compressScale->setEnabled(sizeLike);
    m_compressTargetSize->setEnabled(sizeLike);
    m_compressTargetUnit->setEnabled(sizeLike);
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
    // 与压缩估算同理：按原图/预览像素比放大，避免大图低估。
    if (m_lastSourceSize.isValid() && !m_lastImage.isNull()) {
        const double origPixels = double(m_lastSourceSize.width()) * m_lastSourceSize.height();
        const double previewPixels = double(m_lastImage.width()) * m_lastImage.height();
        if (previewPixels > 0 && origPixels > previewPixels)
            bytes = qRound64(bytes * (origPixels / previewPixels));
    }
    m_convertEstimateLabel->setText(tr("Estimated size: %1").arg(FileUtils::formatFileSize(bytes)));
}

void PropertyPanel::onWatermarkColorClicked()
{
    QColor c = QColorDialog::getColor(m_watermarkColor, this, tr("Select Watermark Color"));
    if (!c.isValid())
        return;
    m_watermarkColor = c;
    m_watermarkColorBtn->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid gray;").arg(c.name()));
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
    // 图片水印：尺寸可调；位置/平铺/边距引擎现在都支持；旋转仍不支持 → 禁用假控件。
    m_watermarkImageScale->setEnabled(!isText);
    m_watermarkPosition->setEnabled(true);
    m_watermarkTile->setEnabled(true);
    m_watermarkTileSpacing->setEnabled(true);
    m_watermarkMargin->setEnabled(true);
    m_watermarkRotation->setEnabled(isText);
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
    // 估算基于预览图（≤3840 长边）编码 —— 原图更大时按像素数比例放大，
    // 避免对大图严重低估。体积近似线性于像素数，比例缩放是合理近似。
    if (m_lastSourceSize.isValid() && !m_lastImage.isNull()) {
        const double origPixels = double(m_lastSourceSize.width()) * m_lastSourceSize.height();
        const double previewPixels = double(m_lastImage.width()) * m_lastImage.height();
        if (previewPixels > 0 && origPixels > previewPixels)
            bytes = qRound64(bytes * (origPixels / previewPixels));
    }
    m_compressEstimateLabel->setText(
        tr("Estimated size: %1").arg(FileUtils::formatFileSize(bytes)));
}

void PropertyPanel::onEditFontColorClicked()
{
    QColor c = QColorDialog::getColor(m_editTextColor, this, tr("Select Text Color"));
    if (!c.isValid())
        return;
    m_editTextColor = c;
    m_editTextColorBtn->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid gray;").arg(c.name()));
    emit settingsChanged();
}

void PropertyPanel::onEditToolChanged(int index)
{
    Q_UNUSED(index)
    EditToolType tool = static_cast<EditToolType>(m_editTool->currentData().toInt());
    bool isShape = (tool == EditToolType::Rectangle || tool == EditToolType::Ellipse);
    bool isText = (tool == EditToolType::Text);
    bool isFilter = (tool == EditToolType::Filter);
    bool isMosaic = (tool == EditToolType::Mosaic);

    m_editFilterType->setEnabled(isFilter);
    if (m_editFilterType->parentWidget())
        m_editFilterType->parentWidget()->setEnabled(isFilter);
    m_editFilterStrength->setEnabled(isFilter);
    if (m_editFilterStrength->parentWidget())
        m_editFilterStrength->parentWidget()->setEnabled(isFilter);
    m_editMosaicStyle->setEnabled(isMosaic);
    if (m_editMosaicStyle->parentWidget())
        m_editMosaicStyle->parentWidget()->setEnabled(isMosaic);
    m_editMosaicSize->setEnabled(isMosaic);
    if (m_editMosaicSize->parentWidget())
        m_editMosaicSize->parentWidget()->setEnabled(isMosaic);
    m_editColor->setEnabled(!isFilter && !isMosaic);
    if (m_editColor->parentWidget())
        m_editColor->parentWidget()->setEnabled(!isFilter && !isMosaic);
    m_editFillStyle->setEnabled(isShape);
    if (m_editFillStyle->parentWidget())
        m_editFillStyle->parentWidget()->setEnabled(isShape);
    m_editFontFamily->setEnabled(isText);
    if (m_editFontFamily->parentWidget())
        m_editFontFamily->parentWidget()->setEnabled(isText);
    m_editFontSize->setEnabled(isText);
    if (m_editFontSize->parentWidget())
        m_editFontSize->parentWidget()->setEnabled(isText);
    m_editFontBold->setEnabled(isText);
    if (m_editTextBackground)
        m_editTextBackground->setEnabled(isText);
    if (m_editCropAspectLock)
        m_editCropAspectLock->setEnabled(tool == EditToolType::Crop);
    if (m_editTextColorBtn && m_editTextColorBtn->parentWidget())
        m_editTextColorBtn->parentWidget()->setEnabled(isText);
    m_editLineWidth->setEnabled(!isFilter && !isMosaic && !isText);
    if (m_editLineWidth->parentWidget())
        m_editLineWidth->parentWidget()->setEnabled(!isFilter && !isMosaic && !isText);
    m_editOpacity->setEnabled(!isFilter && !isMosaic);
    if (m_editOpacity->parentWidget())
        m_editOpacity->parentWidget()->setEnabled(!isFilter && !isMosaic);
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
        case EditToolType::Rectangle:
            toolName = tr("Rectangle");
            break;
        case EditToolType::Ellipse:
            toolName = tr("Ellipse");
            break;
        case EditToolType::Arrow:
            toolName = tr("Arrow");
            break;
        case EditToolType::Pen:
            toolName = tr("Pen");
            break;
        case EditToolType::Mosaic:
            toolName = tr("Mosaic");
            break;
        case EditToolType::Text:
            toolName = tr("Text");
            break;
        case EditToolType::Crop:
            toolName = tr("Crop");
            break;
        case EditToolType::Filter:
            toolName = tr("Filter");
            break;
        }
        QString text = QStringLiteral("%1 %2").arg(i + 1).arg(toolName);
        if (a.toolType == EditToolType::Text && !a.text.isEmpty())
            text += QStringLiteral(" - ") + a.text;
        else if (a.toolType == EditToolType::Filter) {
            QString filterName;
            switch (a.filterType) {
            case FilterType::Grayscale:
                filterName = tr("Grayscale");
                break;
            case FilterType::Sepia:
                filterName = tr("Sepia");
                break;
            case FilterType::Warm:
                filterName = tr("Warm");
                break;
            case FilterType::Cool:
                filterName = tr("Cool");
                break;
            case FilterType::HighContrast:
                filterName = tr("High Contrast");
                break;
            case FilterType::Blur:
                filterName = tr("Blur");
                break;
            case FilterType::Sharpen:
                filterName = tr("Sharpen");
                break;
            }
            text += QStringLiteral(" - ") + filterName;
        } else if (a.toolType == EditToolType::Mosaic) {
            QString styleName;
            switch (a.mosaicStyle) {
            case MosaicStyle::Square:
                styleName = tr("Square");
                break;
            case MosaicStyle::Hexagon:
                styleName = tr("Hexagon");
                break;
            case MosaicStyle::Circle:
                styleName = tr("Circle");
                break;
            case MosaicStyle::Blur:
                styleName = tr("Blur");
                break;
            case MosaicStyle::Mezzotint:
                styleName = tr("Mezzotint");
                break;
            case MosaicStyle::ColorHalftone:
                styleName = tr("Color Halftone");
                break;
            }
            text += QStringLiteral(" - ") + styleName;
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
